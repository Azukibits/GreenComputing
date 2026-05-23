#import <Cocoa/Cocoa.h>

#include <algorithm>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

#include "carbon_model.hpp"
#include "energy_estimator.hpp"
#include "function_profile.hpp"
#include "path_utils.hpp"
#include "static_analyzer.hpp"

static NSColor* rgb(int r, int g, int b) {
    return [NSColor colorWithCalibratedRed:r / 255.0
                                     green:g / 255.0
                                      blue:b / 255.0
                                     alpha:1.0];
}

static NSColor* gh_bg() { return rgb(13, 17, 23); }
static NSColor* gh_card() { return rgb(22, 27, 34); }
static NSColor* gh_card_subtle() { return rgb(33, 38, 45); }
static NSColor* gh_border() { return rgb(48, 54, 61); }
static NSColor* gh_text() { return rgb(201, 209, 217); }
static NSColor* gh_muted() { return rgb(139, 148, 158); }
static NSColor* gh_blue() { return rgb(88, 166, 255); }
static NSColor* gh_green() { return rgb(63, 185, 80); }
static NSColor* gh_orange() { return rgb(210, 153, 34); }
static NSColor* gh_red() { return rgb(248, 81, 73); }

static NSString* ns(const std::string& text) {
    return [NSString stringWithUTF8String:text.c_str()];
}

static std::string fmt_co2(double mg) {
    std::ostringstream ss;
    if (mg < 0.001)
        ss << std::fixed << std::setprecision(3) << mg * 1e6 << " ng";
    else if (mg < 1.0)
        ss << std::fixed << std::setprecision(3) << mg * 1000.0 << " ug";
    else if (mg < 1000.0)
        ss << std::fixed << std::setprecision(3) << mg << " mg";
    else
        ss << std::fixed << std::setprecision(4) << mg / 1000.0 << " g";
    return ss.str();
}

static std::string fmt_energy(double j) {
    std::ostringstream ss;
    if (j < 1e-9)
        ss << std::fixed << std::setprecision(2) << j * 1e12 << " pJ";
    else if (j < 1e-6)
        ss << std::fixed << std::setprecision(2) << j * 1e9 << " nJ";
    else if (j < 1e-3)
        ss << std::fixed << std::setprecision(2) << j * 1e6 << " uJ";
    else if (j < 1.0)
        ss << std::fixed << std::setprecision(2) << j * 1e3 << " mJ";
    else
        ss << std::fixed << std::setprecision(3) << j << " J";
    return ss.str();
}

static std::string shorten(const std::string& s, size_t max_len) {
    if (s.size() <= max_len)
        return s;
    if (max_len <= 3)
        return s.substr(0, max_len);
    return s.substr(0, max_len - 3) + "...";
}

static std::vector<std::filesystem::path> app_search_hints() {
    std::vector<std::filesystem::path> hints = {
        std::filesystem::current_path()
    };
    NSString* executable = [NSBundle mainBundle].executablePath;
    if (executable != nil)
        hints.push_back(std::filesystem::path(executable.UTF8String).parent_path());
    return hints;
}

static NSTextField* label(NSRect frame, NSString* text, CGFloat size = 13.0) {
    NSTextField* view = [[NSTextField alloc] initWithFrame:frame];
    view.stringValue = text;
    view.editable = NO;
    view.bezeled = NO;
    view.drawsBackground = NO;
    view.font = [NSFont systemFontOfSize:size];
    view.textColor = gh_text();
    return view;
}

static NSTextField* value_label(NSRect frame) {
    NSTextField* view = label(frame, @"-", 16.0);
    view.font = [NSFont monospacedDigitSystemFontOfSize:16.0 weight:NSFontWeightSemibold];
    view.textColor = gh_text();
    return view;
}

static CGFloat visible_document_width(NSView* view, CGFloat min_width) {
    NSScrollView* scroll = view.enclosingScrollView;
    if (scroll)
        return std::max<CGFloat>(min_width, scroll.contentView.bounds.size.width);
    return std::max<CGFloat>(min_width, view.bounds.size.width);
}

static CGFloat visible_document_height(NSView* view, CGFloat min_height) {
    NSScrollView* scroll = view.enclosingScrollView;
    if (scroll)
        return std::max<CGFloat>(min_height, scroll.contentView.bounds.size.height);
    return std::max<CGFloat>(min_height, view.bounds.size.height);
}

@interface CarbonChartView : NSView
@property(nonatomic, assign) ProgramProfile* program;
@property(nonatomic, assign) NSInteger selectedRow;
@property(nonatomic, assign) BOOL english;
@property(nonatomic, weak) id target;
@property(nonatomic, assign) SEL action;
- (void)reloadData;
@end

@implementation CarbonChartView

- (instancetype)initWithFrame:(NSRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        _selectedRow = -1;
        self.wantsLayer = YES;
        self.layer.backgroundColor = gh_card().CGColor;
    }
    return self;
}

- (BOOL)isFlipped {
    return YES;
}

- (void)reloadData {
    NSInteger count = _program ? (NSInteger)_program->functions.size() : 0;
    CGFloat width = std::max<CGFloat>(visible_document_width(self, 640.0),
                                      112.0 + std::max<NSInteger>(count - 1, 0) * 92.0);
    [self setFrameSize:NSMakeSize(width, visible_document_height(self, 240.0))];
    [self setNeedsDisplay:YES];
}

- (void)drawRect:(NSRect)rect {
    (void)rect;
    [gh_card() setFill];
    NSRectFill(self.bounds);

    NSDictionary* title_attr = @{
        NSFontAttributeName: [NSFont systemFontOfSize:14.0 weight:NSFontWeightSemibold],
        NSForegroundColorAttributeName: gh_text()
    };
    NSDictionary* muted_attr = @{
        NSFontAttributeName: [NSFont systemFontOfSize:12.0],
        NSForegroundColorAttributeName: gh_muted()
    };
    NSDictionary* name_attr = @{
        NSFontAttributeName: [NSFont systemFontOfSize:12.0 weight:NSFontWeightMedium],
        NSForegroundColorAttributeName: gh_text()
    };
    NSDictionary* value_attr = @{
        NSFontAttributeName: [NSFont monospacedDigitSystemFontOfSize:12.0 weight:NSFontWeightRegular],
        NSForegroundColorAttributeName: gh_muted()
    };

    NSString* chart_title = _english ? @"Function Carbon Chart" : @"函数碳排放折线图";
    NSString* chart_note = _english ? @"All functions are shown on one chart with nonlinear scaling"
                                    : @"所有函数统一显示在一张折线图中，采用非线性刻度";
    [chart_title drawAtPoint:NSMakePoint(16, 14) withAttributes:title_attr];
    [chart_note drawAtPoint:NSMakePoint(_english ? 176 : 150, 16) withAttributes:muted_attr];

    if (!_program || _program->functions.empty()) {
        NSString* empty = _english ? @"Run analysis to show per-function carbon output here."
                                   : @"完成分析后在这里显示每个函数的碳排放。";
        [empty drawAtPoint:NSMakePoint(16, 58) withAttributes:muted_attr];
        return;
    }

    std::vector<NSInteger> plotted;
    for (NSInteger i = 0; i < (NSInteger)_program->functions.size(); ++i)
        plotted.push_back(i);
    std::sort(plotted.begin(), plotted.end(), [&](NSInteger a, NSInteger b) {
        return _program->functions[(size_t)a].name < _program->functions[(size_t)b].name;
    });
    if (plotted.empty()) {
        NSString* empty = _english ? @"No functions available to draw."
                                   : @"没有可绘制的函数。";
        [empty drawAtPoint:NSMakePoint(16, 58) withAttributes:muted_attr];
        return;
    }

    double max_co2 = 0.0;
    double min_positive = 0.0;
    for (NSInteger idx : plotted) {
        double co2 = _program->functions[(size_t)idx].estimated_co2_mg;
        if (co2 > 0.0 && (min_positive <= 0.0 || co2 < min_positive))
            min_positive = co2;
        max_co2 = std::max(max_co2, _program->functions[(size_t)idx].estimated_co2_mg);
    }
    if (max_co2 <= 0.0)
        max_co2 = 1.0;
    if (min_positive <= 0.0)
        min_positive = max_co2 * 0.01;
    double floor_co2 = min_positive * 0.25;
    double log_min = std::log10(floor_co2);
    double log_max = std::log10(max_co2 + floor_co2);
    double log_span = log_max - log_min;
    auto scaled_value = [&](double co2) {
        if (log_span <= 1e-9)
            return std::clamp(co2 / max_co2, 0.0, 1.0);
        return std::clamp((std::log10(co2 + floor_co2) - log_min) / log_span, 0.0, 1.0);
    };

    const NSInteger count = (NSInteger)plotted.size();
    const CGFloat left = 58.0;
    const CGFloat right = 30.0;
    const CGFloat top = 52.0;
    const CGFloat plot_h = std::max<CGFloat>(96.0, self.bounds.size.height - top - 94.0);
    const CGFloat step = count > 1 ? std::max<CGFloat>(56.0, (self.bounds.size.width - left - right) / (count - 1)) : 0.0;
    const CGFloat axis_y = top + plot_h;

    if (_selectedRow >= 0) {
        auto it = std::find(plotted.begin(), plotted.end(), _selectedRow);
        if (it != plotted.end()) {
            CGFloat x = left + (it - plotted.begin()) * step;
            [[gh_blue() colorWithAlphaComponent:0.12] setFill];
            NSBezierPath* selected = [NSBezierPath bezierPathWithRoundedRect:NSMakeRect(x - 42, 42, 84, self.bounds.size.height - 52.0)
                                                                     xRadius:8
                                                                     yRadius:8];
            [selected fill];

            [[gh_blue() colorWithAlphaComponent:0.35] setStroke];
            NSBezierPath* guide = [NSBezierPath bezierPath];
            [guide moveToPoint:NSMakePoint(x, top)];
            [guide lineToPoint:NSMakePoint(x, axis_y)];
            CGFloat dash[] = {4.0, 4.0};
            [guide setLineDash:dash count:2 phase:0.0];
            [guide setLineWidth:1.5];
            [guide stroke];
        }
    }

    [gh_border() setStroke];
    for (int i = 0; i <= 3; ++i) {
        CGFloat y = top + plot_h * i / 3.0;
        NSBezierPath* grid = [NSBezierPath bezierPath];
        [grid moveToPoint:NSMakePoint(left - 8, y)];
        [grid lineToPoint:NSMakePoint(self.bounds.size.width - 24, y)];
        [grid setLineWidth:1.0];
        [grid stroke];
    }

    NSBezierPath* line = [NSBezierPath bezierPath];
    for (NSInteger i = 0; i < count; ++i) {
        const auto& fp = _program->functions[(size_t)plotted[(size_t)i]];
        double pct = scaled_value(fp.estimated_co2_mg);
        CGFloat x = left + i * step;
        CGFloat y = top + (1.0 - (CGFloat)pct) * plot_h;
        if (i == 0)
            [line moveToPoint:NSMakePoint(x, y)];
        else
            [line lineToPoint:NSMakePoint(x, y)];
    }
    [gh_blue() setStroke];
    [line setLineWidth:2.0];
    [line stroke];

    for (NSInteger i = 0; i < count; ++i) {
        NSInteger idx = plotted[(size_t)i];
        const auto& fp = _program->functions[(size_t)idx];
        double pct = scaled_value(fp.estimated_co2_mg);
        double raw_pct = std::clamp(fp.estimated_co2_mg / max_co2, 0.0, 1.0);
        CGFloat x = left + i * step;
        CGFloat y = top + (1.0 - (CGFloat)pct) * plot_h;

        NSColor* point_color = raw_pct > 0.6 ? gh_red() : (raw_pct > 0.3 ? gh_orange() : gh_green());
        [point_color setFill];
        NSBezierPath* point = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(x - 5, y - 5, 10, 10)];
        [point fill];

        if (idx == _selectedRow) {
            [[gh_blue() colorWithAlphaComponent:0.18] setFill];
            NSBezierPath* halo = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(x - 12, y - 12, 24, 24)];
            [halo fill];

            [gh_text() setFill];
            NSBezierPath* selected_point = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(x - 6, y - 6, 12, 12)];
            [selected_point fill];

            [gh_blue() setStroke];
            NSBezierPath* ring = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(x - 10, y - 10, 20, 20)];
            [ring setLineWidth:2.5];
            [ring stroke];
        }

        [ns(fmt_co2(fp.estimated_co2_mg)) drawAtPoint:NSMakePoint(x - 34, std::max<CGFloat>(36.0, y - 24))
                                      withAttributes:value_attr];

        std::string name = shorten(fp.name, 12);
        NSRect name_rect = NSMakeRect(x - 40, axis_y + 16, 80, 34);
        [ns(name) drawInRect:name_rect withAttributes:name_attr];
    }
}

- (void)mouseDown:(NSEvent*)event {
    NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    if (!_program || _program->functions.empty())
        return;

    std::vector<NSInteger> plotted;
    for (NSInteger i = 0; i < (NSInteger)_program->functions.size(); ++i)
        plotted.push_back(i);
    std::sort(plotted.begin(), plotted.end(), [&](NSInteger a, NSInteger b) {
        return _program->functions[(size_t)a].name < _program->functions[(size_t)b].name;
    });

    const CGFloat left = 58.0;
    const CGFloat right = 30.0;
    const NSInteger count = (NSInteger)plotted.size();
    if (count <= 0)
        return;

    double max_co2 = 0.0;
    double min_positive = 0.0;
    for (NSInteger idx : plotted) {
        double co2 = _program->functions[(size_t)idx].estimated_co2_mg;
        if (co2 > 0.0 && (min_positive <= 0.0 || co2 < min_positive))
            min_positive = co2;
        max_co2 = std::max(max_co2, co2);
    }
    if (max_co2 <= 0.0)
        max_co2 = 1.0;
    if (min_positive <= 0.0)
        min_positive = max_co2 * 0.01;
    double floor_co2 = min_positive * 0.25;
    double log_min = std::log10(floor_co2);
    double log_max = std::log10(max_co2 + floor_co2);
    double log_span = log_max - log_min;
    auto scaled_value = [&](double co2) {
        if (log_span <= 1e-9)
            return std::clamp(co2 / max_co2, 0.0, 1.0);
        return std::clamp((std::log10(co2 + floor_co2) - log_min) / log_span, 0.0, 1.0);
    };

    const CGFloat top = 52.0;
    const CGFloat plot_h = std::max<CGFloat>(96.0, self.bounds.size.height - top - 94.0);
    const CGFloat axis_y = top + plot_h;
    const CGFloat step = count > 1
        ? std::max<CGFloat>(56.0, (self.bounds.size.width - left - right) / (count - 1))
        : 0.0;

    NSInteger matched_row = -1;
    double best_score = std::numeric_limits<double>::infinity();
    for (NSInteger i = 0; i < count; ++i) {
        NSInteger idx = plotted[(size_t)i];
        const auto& fp = _program->functions[(size_t)idx];
        CGFloat x = left + i * step;
        CGFloat y = top + (1.0 - (CGFloat)scaled_value(fp.estimated_co2_mg)) * plot_h;

        NSRect point_rect = NSInsetRect(NSMakeRect(x - 9, y - 9, 18, 18), -5, -5);
        NSRect value_rect = NSMakeRect(x - 40, std::max<CGFloat>(32.0, y - 28), 80, 20);
        NSRect name_rect = NSMakeRect(x - 44, axis_y + 12, 88, 40);

        double score = std::numeric_limits<double>::infinity();
        if (NSPointInRect(p, point_rect)) {
            score = 0.0;
        } else if (NSPointInRect(p, value_rect)) {
            score = 10.0;
        } else if (NSPointInRect(p, name_rect)) {
            score = 20.0;
        } else {
            const double dx = p.x - x;
            const double dy = p.y - y;
            const double dist2 = dx * dx + dy * dy;
            if (dist2 <= 26.0 * 26.0)
                score = 100.0 + dist2;
        }

        if (score < best_score) {
            best_score = score;
            matched_row = idx;
        }
    }

    if (matched_row >= 0) {
        _selectedRow = matched_row;
        [self setNeedsDisplay:YES];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Warc-performSelector-leaks"
        if (_target && _action && [_target respondsToSelector:_action])
            [_target performSelector:_action withObject:self];
#pragma clang diagnostic pop
    }
}

@end

@interface CarbonDetailView : NSView
@property(nonatomic, assign) ProgramProfile* program;
@property(nonatomic, assign) NSInteger selectedRow;
@property(nonatomic, assign) BOOL english;
@property(nonatomic, copy) NSString* message;
- (void)reloadData;
@end

@implementation CarbonDetailView

- (instancetype)initWithFrame:(NSRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        _selectedRow = -1;
        _message = @"选择源文件或项目目录并点击“开始分析”。";
        self.wantsLayer = YES;
        self.layer.backgroundColor = gh_card().CGColor;
    }
    return self;
}

- (BOOL)isFlipped {
    return YES;
}

- (void)reloadData {
    CGFloat width = visible_document_width(self, 640.0);
    CGFloat height = 250.0;
    if (_program && _selectedRow >= 0 && _selectedRow < (NSInteger)_program->functions.size()) {
        const auto& fp = _program->functions[(size_t)_selectedRow];
        const CGFloat content_w = std::max<CGFloat>(320.0, width - 36.0);
        const CGFloat chip_gap = 14.0;
        NSInteger chip_cols = std::clamp<NSInteger>((NSInteger)((content_w + chip_gap) / (190.0 + chip_gap)), 2, 4);
        NSInteger chip_rows = (7 + chip_cols - 1) / chip_cols;
        height = 258.0 + chip_rows * 84.0 + 40.0;
        if (!fp.callees.empty())
            height += 72.0 + std::min<size_t>(fp.callees.size(), 8) * 46.0;
        if (!fp.warnings.empty() || !fp.suggestions.empty())
            height += 52.0 + std::min<size_t>(fp.warnings.size() + fp.suggestions.size(), 6) * 22.0;
    }
    [self setFrameSize:NSMakeSize(width, height)];
    [self setNeedsDisplay:YES];
}

- (void)drawMetric:(NSString*)title
             value:(NSString*)value
             frame:(NSRect)frame
             color:(NSColor*)color {
    [gh_card_subtle() setFill];
    NSBezierPath* box = [NSBezierPath bezierPathWithRoundedRect:frame xRadius:8 yRadius:8];
    [box fill];
    [gh_border() setStroke];
    [box setLineWidth:1.0];
    [box stroke];

    NSDictionary* title_attr = @{
        NSFontAttributeName: [NSFont systemFontOfSize:11.0],
        NSForegroundColorAttributeName: gh_muted()
    };
    NSDictionary* value_attr = @{
        NSFontAttributeName: [NSFont monospacedDigitSystemFontOfSize:15.0 weight:NSFontWeightSemibold],
        NSForegroundColorAttributeName: color
    };
    [title drawAtPoint:NSMakePoint(frame.origin.x + 12, frame.origin.y + 9) withAttributes:title_attr];
    [value drawAtPoint:NSMakePoint(frame.origin.x + 12, frame.origin.y + 30) withAttributes:value_attr];
}

- (void)drawRect:(NSRect)rect {
    (void)rect;
    [gh_card() setFill];
    NSRectFill(self.bounds);

    NSDictionary* title_attr = @{
        NSFontAttributeName: [NSFont systemFontOfSize:15.0 weight:NSFontWeightSemibold],
        NSForegroundColorAttributeName: gh_text()
    };
    NSDictionary* text_attr = @{
        NSFontAttributeName: [NSFont systemFontOfSize:12.0],
        NSForegroundColorAttributeName: gh_text()
    };
    NSDictionary* muted_attr = @{
        NSFontAttributeName: [NSFont systemFontOfSize:12.0],
        NSForegroundColorAttributeName: gh_muted()
    };
    NSDictionary* mono_attr = @{
        NSFontAttributeName: [NSFont monospacedDigitSystemFontOfSize:12.0 weight:NSFontWeightRegular],
        NSForegroundColorAttributeName: gh_muted()
    };

    if (!_program || _selectedRow < 0 || _selectedRow >= (NSInteger)_program->functions.size()) {
        NSString* msg = _message ? _message : (_english ? @"Select a function to show details." : @"选择函数后显示详情。");
        [msg drawAtPoint:NSMakePoint(18, 18) withAttributes:muted_attr];
        return;
    }

    const auto& fp = _program->functions[(size_t)_selectedRow];
    const CGFloat content_w = std::max<CGFloat>(320.0, self.bounds.size.width - 36.0);
    [(_english ? @"Function Details" : @"函数详情") drawAtPoint:NSMakePoint(18, 16) withAttributes:title_attr];
    [ns(fp.name) drawAtPoint:NSMakePoint(18, 42)
              withAttributes:@{
                  NSFontAttributeName: [NSFont systemFontOfSize:18.0 weight:NSFontWeightSemibold],
                  NSForegroundColorAttributeName: gh_blue()
              }];

    std::ostringstream location;
    location << fp.file << ":" << fp.line_start << "-" << fp.line_end;
    if (!fp.language.empty())
        location << "  " << (_english ? "Language " : "语言 ") << fp.language;
    [ns(shorten(location.str(), (size_t)std::max<CGFloat>(48.0, content_w / 7.0)))
        drawAtPoint:NSMakePoint(18, 70)
     withAttributes:muted_attr];

    const CGFloat metric_gap = 16.0;
    const CGFloat metric_w = (content_w - metric_gap * 3.0) / 4.0;
    [self drawMetric:_english ? @"Carbon" : @"碳排放"
               value:ns(fmt_co2(fp.estimated_co2_mg) + " CO2eq")
               frame:NSMakeRect(18, 102, metric_w, 66)
               color:gh_green()];
    [self drawMetric:_english ? @"Energy" : @"能耗"
               value:ns(fmt_energy(fp.estimated_joules))
               frame:NSMakeRect(18 + (metric_w + metric_gap), 102, metric_w, 66)
               color:gh_blue()];
    [self drawMetric:_english ? @"Score" : @"能耗评分"
               value:ns(std::to_string((long long)fp.energy_score))
               frame:NSMakeRect(18 + (metric_w + metric_gap) * 2.0, 102, metric_w, 66)
               color:gh_text()];
    std::ostringstream loop_text;
    if (_english)
        loop_text << "Depth " << fp.loops.depth << " / " << fp.loops.count;
    else
        loop_text << "深度 " << fp.loops.depth << " / " << fp.loops.count << " 个";
    [self drawMetric:_english ? @"Loops" : @"循环"
               value:ns(loop_text.str())
               frame:NSMakeRect(18 + (metric_w + metric_gap) * 3.0, 102, metric_w, 66)
               color:fp.loops.depth >= 2 ? gh_orange() : gh_text()];

    struct Part {
        const char* label;
        uint64_t count;
        double weight;
        NSColor* color;
    };
    const std::vector<Part> parts = {
        {"ALU", fp.raw.alu, weight_of(InstrCat::ALU), gh_blue()},
        {_english ? "FPU" : "浮点", fp.raw.fpu, weight_of(InstrCat::FPU), gh_orange()},
        {_english ? "Memory" : "内存", fp.raw.memory, weight_of(InstrCat::MEMORY), gh_red()},
        {_english ? "Branch" : "分支", fp.raw.branch, weight_of(InstrCat::BRANCH), gh_muted()},
        {"IO", fp.raw.io, weight_of(InstrCat::IO), gh_red()},
        {"SIMD", fp.raw.simd, weight_of(InstrCat::SIMD), gh_green()},
        {_english ? "Sync" : "同步", fp.raw.atomic, weight_of(InstrCat::ATOMIC), gh_orange()},
    };

    double total = 0.0;
    for (const auto& part : parts)
        total += part.count * part.weight;
    if (total <= 0.0)
        total = 1.0;

    [(_english ? @"Instruction Contribution" : @"指令贡献拆分")
        drawAtPoint:NSMakePoint(18, 194)
     withAttributes:title_attr];
    [(_english ? @"Estimated by category weights; runtime sampling is not included" : @"按类别权重估算，不包含真实运行采样")
        drawAtPoint:NSMakePoint(_english ? 176 : 122, 196)
     withAttributes:muted_attr];

    CGFloat stack_x = 18.0;
    CGFloat stack_y = 226.0;
    CGFloat stack_w = content_w;
    CGFloat stack_h = 14.0;
    [gh_border() setFill];
    NSBezierPath* stack_track = [NSBezierPath bezierPathWithRoundedRect:NSMakeRect(stack_x, stack_y, stack_w, stack_h)
                                                                xRadius:7
                                                                yRadius:7];
    [stack_track fill];

    CGFloat cursor_x = stack_x;
    for (const auto& part : parts) {
        double weighted = part.count * part.weight;
        double share = weighted / total;
        if (weighted <= 0.0)
            continue;
        CGFloat width = std::max<CGFloat>(4.0, stack_w * share);
        [part.color setFill];
        NSBezierPath* segment = [NSBezierPath bezierPathWithRoundedRect:NSMakeRect(cursor_x, stack_y, width, stack_h)
                                                                 xRadius:7
                                                                 yRadius:7];
        [segment fill];
        cursor_x += width;
    }

    CGFloat chip_gap = 14.0;
    NSInteger chip_cols = std::clamp<NSInteger>((NSInteger)((content_w + chip_gap) / (190.0 + chip_gap)), 2, 4);
    CGFloat chip_w = (content_w - (chip_cols - 1) * chip_gap) / chip_cols;
    CGFloat chip_h = 74.0;
    for (size_t i = 0; i < parts.size(); ++i) {
        const auto& part = parts[i];
        double weighted = part.count * part.weight;
        double share = weighted / total;
        double co2 = fp.estimated_co2_mg * share;
        CGFloat x = 18.0 + (CGFloat)(i % (size_t)chip_cols) * (chip_w + chip_gap);
        CGFloat y = 258.0 + (CGFloat)(i / (size_t)chip_cols) * 84.0;

        [gh_card_subtle() setFill];
        NSBezierPath* chip = [NSBezierPath bezierPathWithRoundedRect:NSMakeRect(x, y, chip_w, chip_h)
                                                             xRadius:8
                                                             yRadius:8];
        [chip fill];
        [gh_border() setStroke];
        [chip setLineWidth:1.0];
        [chip stroke];

        [part.color setFill];
        NSBezierPath* dot = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(x + 10, y + 12, 8, 8)];
        [dot fill];
        [ns(part.label) drawAtPoint:NSMakePoint(x + 26, y + 9) withAttributes:text_attr];

        std::ostringstream top_line;
        top_line << part.count << " / " << std::fixed << std::setprecision(1) << share * 100.0 << "%";
        [ns(top_line.str()) drawAtPoint:NSMakePoint(x + 10, y + 32) withAttributes:mono_attr];
        [ns(fmt_co2(co2) + " CO2eq") drawAtPoint:NSMakePoint(x + 10, y + 50) withAttributes:mono_attr];
    }

    CGFloat section_y = 258.0 + ((parts.size() + (size_t)chip_cols - 1) / (size_t)chip_cols) * 84.0 + 16.0;
    if (!fp.callees.empty()) {
        [(_english ? @"Call Distribution" : @"调用分布") drawAtPoint:NSMakePoint(18, section_y) withAttributes:title_attr];
        [(_english ? @"Estimated carbon output of called functions" : @"被调用函数的估算碳排放，不计入当前函数自身指令拆分")
            drawAtPoint:NSMakePoint(_english ? 134 : 86, section_y + 2)
         withAttributes:muted_attr];
        section_y += 32.0;

        struct CallItem {
            std::string name;
            double co2;
            double energy;
            bool known;
        };
        std::vector<CallItem> calls;
        double max_call_co2 = 0.0;
        for (const auto& callee : fp.callees) {
            auto it = std::find_if(_program->functions.begin(), _program->functions.end(),
                [&](const FunctionProfile& candidate) {
                    return candidate.name == callee;
                });
            if (it == _program->functions.end()) {
                calls.push_back({callee, 0.0, 0.0, false});
            } else {
                calls.push_back({callee, it->estimated_co2_mg, it->estimated_joules, true});
                max_call_co2 = std::max(max_call_co2, it->estimated_co2_mg);
            }
        }
        if (max_call_co2 <= 0.0)
            max_call_co2 = 1.0;

        int shown = 0;
        for (const auto& call : calls) {
            CGFloat row_x = 28.0;
            CGFloat row_w = std::max<CGFloat>(320.0, self.bounds.size.width - 56.0);
            CGFloat row_h = 38.0;
            [gh_bg() setFill];
            NSBezierPath* row_box = [NSBezierPath bezierPathWithRoundedRect:NSMakeRect(row_x, section_y, row_w, row_h)
                                                                     xRadius:8
                                                                     yRadius:8];
            [row_box fill];
            [gh_border() setStroke];
            [row_box setLineWidth:1.0];
            [row_box stroke];

            [gh_blue() setStroke];
            NSBezierPath* link = [NSBezierPath bezierPath];
            [link moveToPoint:NSMakePoint(row_x + 14, section_y + 10)];
            [link lineToPoint:NSMakePoint(row_x + 14, section_y + row_h - 10)];
            [link lineToPoint:NSMakePoint(row_x + 28, section_y + row_h - 10)];
            [link setLineWidth:1.5];
            [link stroke];

            [ns(shorten(call.name, 28)) drawAtPoint:NSMakePoint(row_x + 42, section_y + 10)
                                     withAttributes:text_attr];

            CGFloat spark_x = row_x + std::min<CGFloat>(250.0, row_w * 0.38);
            CGFloat value_x = row_x + row_w - 178.0;
            CGFloat spark_w = std::max<CGFloat>(90.0, value_x - spark_x - 22.0);
            CGFloat spark_y = section_y + 15.0;
            [gh_border() setFill];
            NSBezierPath* track = [NSBezierPath bezierPathWithRoundedRect:NSMakeRect(spark_x, spark_y, spark_w, 8)
                                                                   xRadius:4
                                                                   yRadius:4];
            [track fill];
            if (call.known) {
                CGFloat fill_w = std::max<CGFloat>(3.0, spark_w * std::clamp(call.co2 / max_call_co2, 0.0, 1.0));
                [gh_blue() setFill];
                NSBezierPath* fill = [NSBezierPath bezierPathWithRoundedRect:NSMakeRect(spark_x, spark_y, fill_w, 8)
                                                                      xRadius:4
                                                                      yRadius:4];
                [fill fill];
            }

            std::string value = call.known ? fmt_co2(call.co2) + " CO2eq" : (_english ? "Unknown" : "未估算");
            [ns(value) drawAtPoint:NSMakePoint(value_x, section_y + 10)
                    withAttributes:mono_attr];

            section_y += 46.0;
            if (++shown >= 8)
                break;
        }
        if (calls.size() > 8)
            [ns((_english ? "Hidden calls: " : "还有 ") + std::to_string(calls.size() - 8) + (_english ? "" : " 个调用未显示"))
                drawAtPoint:NSMakePoint(28, section_y)
             withAttributes:muted_attr];
        section_y += 22.0;
    }

    if (!fp.warnings.empty() || !fp.suggestions.empty()) {
        [(_english ? @"Notes" : @"提示") drawAtPoint:NSMakePoint(18, section_y) withAttributes:title_attr];
        section_y += 28.0;
        int shown = 0;
        for (const auto& item : fp.warnings) {
            [ns((_english ? "Warning: " : "警告: ") + item) drawAtPoint:NSMakePoint(28, section_y)
                              withAttributes:@{
                                  NSFontAttributeName: [NSFont systemFontOfSize:12.0],
                                  NSForegroundColorAttributeName: gh_red()
                              }];
            section_y += 22.0;
            if (++shown >= 6)
                break;
        }
        for (const auto& item : fp.suggestions) {
            if (shown >= 6)
                break;
            [ns((_english ? "Suggestion: " : "建议: ") + item) drawAtPoint:NSMakePoint(28, section_y)
                              withAttributes:@{
                                  NSFontAttributeName: [NSFont systemFontOfSize:12.0],
                                  NSForegroundColorAttributeName: gh_green()
                              }];
            section_y += 22.0;
            ++shown;
        }
    }
}

@end

@interface AppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate>
@end

@implementation AppDelegate {
    NSWindow* window_;
    NSTextField* title_;
    NSTextField* subtitle_;
    NSView* side_;
    NSTextField* config_title_;
    NSTextField* language_title_;
    NSPopUpButton* language_popup_;
    NSTextField* source_title_;
    NSTextField* file_field_;
    NSButton* browse_button_;
    NSTextField* hw_title_;
    NSPopUpButton* hw_popup_;
    NSTextField* grid_title_;
    NSPopUpButton* grid_popup_;
    NSButton* analyze_button_;
    NSTextField* status_label_;
    NSView* summary_;
    NSTextField* co2_title_;
    NSTextField* co2_label_;
    NSTextField* energy_title_;
    NSTextField* energy_label_;
    NSTextField* count_title_;
    NSTextField* count_label_;
    NSScrollView* chart_scroll_;
    CarbonChartView* chart_view_;
    NSScrollView* detail_scroll_;
    CarbonDetailView* detail_view_;

    std::vector<std::string> hw_keys_;
    std::vector<std::string> grid_keys_;
    std::string source_path_;
    ProgramProfile program_;
    BOOL english_;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        english_ = NO;
        hw_keys_ = HARDWARE_PROFILE_KEYS;
        grid_keys_ = {
            "cn", "us", "us_ca", "us_tx", "eu", "de", "fr",
            "no", "uk", "jp", "au", "br", "in", "global"
        };
    }
    return self;
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    (void)notification;

    NSRect frame = NSMakeRect(0, 0, 1180, 760);
    window_ = [[NSWindow alloc] initWithContentRect:frame
                                          styleMask:NSWindowStyleMaskTitled |
                                                    NSWindowStyleMaskClosable |
                                                    NSWindowStyleMaskMiniaturizable |
                                                    NSWindowStyleMaskResizable
                                            backing:NSBackingStoreBuffered
                                              defer:NO];
    window_.title = @"GreenComputing 碳排放静态分析器";
    window_.minSize = NSMakeSize(1040, 660);
    window_.delegate = self;
    [window_ center];

    NSView* root = window_.contentView;
    root.wantsLayer = YES;
    root.layer.backgroundColor = gh_bg().CGColor;

    title_ = label(NSMakeRect(24, 714, 280, 28), @"GreenComputing", 22.0);
    title_.font = [NSFont systemFontOfSize:22.0 weight:NSFontWeightSemibold];
    title_.textColor = gh_text();
    [root addSubview:title_];
    subtitle_ = label(NSMakeRect(216, 718, 220, 20), @"碳排放静态分析器", 13.0);
    subtitle_.textColor = gh_muted();
    [root addSubview:subtitle_];

    side_ = [[NSView alloc] initWithFrame:NSMakeRect(24, 24, 292, 672)];
    side_.wantsLayer = YES;
    side_.layer.backgroundColor = gh_card().CGColor;
    side_.layer.borderColor = gh_border().CGColor;
    side_.layer.borderWidth = 1.0;
    side_.layer.cornerRadius = 8.0;
    [root addSubview:side_];

    config_title_ = label(NSMakeRect(44, 654, 120, 22), @"分析配置", 15.0);
    config_title_.font = [NSFont systemFontOfSize:15.0 weight:NSFontWeightSemibold];
    config_title_.textColor = gh_text();
    [root addSubview:config_title_];

    language_title_ = label(NSMakeRect(950, 718, 70, 20), @"语言", 13.0);
    [root addSubview:language_title_];
    language_popup_ = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(1014, 714, 130, 30)];
    [language_popup_ addItemWithTitle:@"中文"];
    [language_popup_ addItemWithTitle:@"English"];
    language_popup_.target = self;
    language_popup_.action = @selector(languageChanged:);
    [root addSubview:language_popup_];

    source_title_ = label(NSMakeRect(44, 604, 120, 20), @"源路径", 13.0);
    [root addSubview:source_title_];
    file_field_ = [[NSTextField alloc] initWithFrame:NSMakeRect(44, 574, 188, 28)];
    {
        const std::string demo_path = find_demo_path(app_search_hints());
        source_path_ = demo_path.empty() ? "demo.cpp" : demo_path;
        file_field_.stringValue = ns(compact_input_path_label(source_path_));
    }
    file_field_.textColor = gh_text();
    file_field_.backgroundColor = gh_bg();
    [root addSubview:file_field_];

    browse_button_ = [[NSButton alloc] initWithFrame:NSMakeRect(238, 574, 58, 28)];
    browse_button_.title = @"路径";
    browse_button_.bezelStyle = NSBezelStyleRounded;
    browse_button_.target = self;
    browse_button_.action = @selector(selectFile:);
    [root addSubview:browse_button_];

    hw_title_ = label(NSMakeRect(44, 526, 120, 20), @"硬件配置", 13.0);
    [root addSubview:hw_title_];
    hw_popup_ = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(44, 494, 252, 30)];
    for (const auto& key : hw_keys_) {
        const auto it = HARDWARE_PROFILES.find(key);
        [hw_popup_ addItemWithTitle:ns(it != HARDWARE_PROFILES.end() ? it->second.name : key)];
    }
    [hw_popup_ selectItemAtIndex:7];
    [root addSubview:hw_popup_];

    grid_title_ = label(NSMakeRect(44, 446, 120, 20), @"电网区域", 13.0);
    [root addSubview:grid_title_];
    grid_popup_ = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(44, 414, 252, 30)];
    for (const auto& key : grid_keys_) {
        const auto it = GRID_REGIONS.find(key);
        if (it != GRID_REGIONS.end()) {
            std::ostringstream ss;
            ss << it->second.name << " (" << (int)it->second.carbon_intensity << ")";
            [grid_popup_ addItemWithTitle:ns(ss.str())];
        } else {
            [grid_popup_ addItemWithTitle:ns(key)];
        }
    }
    [grid_popup_ selectItemAtIndex:13];
    [root addSubview:grid_popup_];

    analyze_button_ = [[NSButton alloc] initWithFrame:NSMakeRect(44, 354, 252, 34)];
    analyze_button_.title = @"开始分析";
    analyze_button_.bezelStyle = NSBezelStyleRounded;
    analyze_button_.contentTintColor = gh_blue();
    analyze_button_.target = self;
    analyze_button_.action = @selector(runAnalysis:);
    [root addSubview:analyze_button_];

    status_label_ = label(NSMakeRect(44, 306, 252, 40), @"等待分析", 13.0);
    status_label_.textColor = gh_muted();
    status_label_.lineBreakMode = NSLineBreakByWordWrapping;
    [root addSubview:status_label_];

    summary_ = [[NSView alloc] initWithFrame:NSMakeRect(336, 616, 820, 80)];
    summary_.wantsLayer = YES;
    summary_.layer.backgroundColor = gh_card().CGColor;
    summary_.layer.borderColor = gh_border().CGColor;
    summary_.layer.borderWidth = 1.0;
    summary_.layer.cornerRadius = 8.0;
    [root addSubview:summary_];

    co2_title_ = label(NSMakeRect(362, 668, 120, 18), @"总碳排放", 12.0);
    [root addSubview:co2_title_];
    co2_label_ = value_label(NSMakeRect(362, 640, 190, 24));
    [root addSubview:co2_label_];

    energy_title_ = label(NSMakeRect(604, 668, 120, 18), @"总能耗", 12.0);
    [root addSubview:energy_title_];
    energy_label_ = value_label(NSMakeRect(604, 640, 190, 24));
    [root addSubview:energy_label_];

    count_title_ = label(NSMakeRect(846, 668, 120, 18), @"函数数", 12.0);
    [root addSubview:count_title_];
    count_label_ = value_label(NSMakeRect(846, 640, 190, 24));
    [root addSubview:count_label_];

    chart_scroll_ = [[NSScrollView alloc] initWithFrame:NSMakeRect(336, 324, 820, 272)];
    chart_scroll_.hasVerticalScroller = YES;
    chart_scroll_.hasHorizontalScroller = YES;
    chart_scroll_.borderType = NSNoBorder;
    chart_scroll_.wantsLayer = YES;
    chart_scroll_.layer.backgroundColor = gh_card().CGColor;
    chart_scroll_.layer.borderColor = gh_border().CGColor;
    chart_scroll_.layer.borderWidth = 1.0;
    chart_scroll_.layer.cornerRadius = 8.0;
    chart_view_ = [[CarbonChartView alloc] initWithFrame:NSMakeRect(0, 0, 820, 272)];
    chart_view_.target = self;
    chart_view_.action = @selector(chartClicked:);
    chart_scroll_.documentView = chart_view_;
    [root addSubview:chart_scroll_];

    detail_scroll_ = [[NSScrollView alloc] initWithFrame:NSMakeRect(336, 24, 820, 280)];
    detail_scroll_.hasVerticalScroller = YES;
    detail_scroll_.borderType = NSNoBorder;
    detail_scroll_.wantsLayer = YES;
    detail_scroll_.layer.backgroundColor = gh_card().CGColor;
    detail_scroll_.layer.borderColor = gh_border().CGColor;
    detail_scroll_.layer.borderWidth = 1.0;
    detail_scroll_.layer.cornerRadius = 8.0;
    detail_view_ = [[CarbonDetailView alloc] initWithFrame:NSMakeRect(0, 0, 820, 250)];
    detail_scroll_.documentView = detail_view_;
    [root addSubview:detail_scroll_];

    [self layoutContent];
    [self retranslate];
    [window_ makeKeyAndOrderFront:nil];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
    (void)sender;
    return YES;
}

- (void)windowDidResize:(NSNotification*)notification {
    (void)notification;
    [self layoutContent];
}

- (void)layoutContent {
    NSView* root = window_.contentView;
    if (!root)
        return;

    const CGFloat width = root.bounds.size.width;
    const CGFloat height = root.bounds.size.height;
    const CGFloat margin = 24.0;
    const CGFloat sidebar_w = 292.0;
    const CGFloat gap = 20.0;
    const CGFloat right_x = margin + sidebar_w + gap;
    const CGFloat right_w = std::max<CGFloat>(640.0, width - right_x - margin);

    title_.frame = NSMakeRect(margin, height - 46.0, 180.0, 28.0);
    subtitle_.frame = NSMakeRect(margin + 192.0, height - 42.0, 220.0, 20.0);
    side_.frame = NSMakeRect(margin, margin, sidebar_w, std::max<CGFloat>(520.0, height - 88.0));

    const CGFloat side_x = margin + 20.0;
    const CGFloat side_w = sidebar_w - 40.0;
    config_title_.frame = NSMakeRect(side_x, height - 106.0, side_w, 22.0);
    language_title_.frame = NSMakeRect(width - 190.0, height - 42.0, 70.0, 20.0);
    language_popup_.frame = NSMakeRect(width - 126.0, height - 46.0, 102.0, 30.0);
    source_title_.frame = NSMakeRect(side_x, height - 156.0, side_w, 20.0);
    file_field_.frame = NSMakeRect(side_x, height - 186.0, side_w - 64.0, 28.0);
    browse_button_.frame = NSMakeRect(side_x + side_w - 58.0, height - 186.0, 58.0, 28.0);
    hw_title_.frame = NSMakeRect(side_x, height - 234.0, side_w, 20.0);
    hw_popup_.frame = NSMakeRect(side_x, height - 266.0, side_w, 30.0);
    grid_title_.frame = NSMakeRect(side_x, height - 314.0, side_w, 20.0);
    grid_popup_.frame = NSMakeRect(side_x, height - 346.0, side_w, 30.0);
    analyze_button_.frame = NSMakeRect(side_x, height - 406.0, side_w, 34.0);
    status_label_.frame = NSMakeRect(side_x, height - 492.0, side_w, 66.0);

    const CGFloat summary_h = 80.0;
    const CGFloat summary_y = height - 144.0;
    summary_.frame = NSMakeRect(right_x, summary_y, right_w, summary_h);

    const CGFloat metric_w = right_w / 3.0;
    NSArray<NSTextField*>* titles = @[ co2_title_, energy_title_, count_title_ ];
    NSArray<NSTextField*>* values = @[ co2_label_, energy_label_, count_label_ ];
    for (NSInteger i = 0; i < 3; ++i) {
        CGFloat x = right_x + 26.0 + metric_w * i;
        [titles[(NSUInteger)i] setFrame:NSMakeRect(x, summary_y + 52.0, metric_w - 42.0, 18.0)];
        [values[(NSUInteger)i] setFrame:NSMakeRect(x, summary_y + 24.0, metric_w - 42.0, 24.0)];
    }

    const CGFloat right_bottom = margin;
    const CGFloat chart_top = summary_y - gap;
    const CGFloat right_available_h = std::max<CGFloat>(420.0, chart_top - right_bottom);
    const CGFloat detail_h = std::max<CGFloat>(220.0, std::floor((right_available_h - gap) * 0.48));
    const CGFloat chart_h = std::max<CGFloat>(220.0, right_available_h - gap - detail_h);
    detail_scroll_.frame = NSMakeRect(right_x, right_bottom, right_w, detail_h);
    chart_scroll_.frame = NSMakeRect(right_x, right_bottom + detail_h + gap, right_w, chart_h);

    [chart_view_ reloadData];
    [detail_view_ reloadData];
}

- (NSString*)textCN:(NSString*)cn en:(NSString*)en {
    return english_ ? en : cn;
}

- (NSString*)hardwareTitleForKey:(const std::string&)key {
    if (!english_) {
        const auto it = HARDWARE_PROFILES.find(key);
        return ns(it != HARDWARE_PROFILES.end() ? it->second.name : key);
    }
    if (key == "rpi4") return @"Raspberry Pi 4";
    if (key == "rpi5") return @"Raspberry Pi 5";
    if (key == "jetson_nano") return @"Jetson Nano";
    if (key == "jetson_orin") return @"Jetson Orin Nano";
    if (key == "mini_pc_n100") return @"Mini PC (Intel N100)";
    if (key == "laptop_low") return @"Laptop Low Power (~15W TDP)";
    if (key == "macbook_air_m2") return @"MacBook Air (M2)";
    if (key == "laptop_mid") return @"Laptop Mid Range (~28W TDP)";
    if (key == "macbook_pro_m3") return @"MacBook Pro (M3)";
    if (key == "laptop_high") return @"Laptop High Performance (~45W TDP)";
    if (key == "desktop_entry") return @"Desktop Entry (~35W TDP)";
    if (key == "desktop_mid") return @"Desktop Mid Range (~65W TDP)";
    if (key == "desktop_high") return @"Desktop High End (~125W TDP)";
    if (key == "workstation_pro") return @"Workstation Pro (~220W TDP)";
    if (key == "server_1u") return @"Server 1U";
    if (key == "server_dual") return @"Server Dual Socket";
    if (key == "server_arm") return @"Server ARM Node";
    if (key == "server_hpc") return @"Server HPC Node";
    return ns(key);
}

- (NSString*)gridTitleForKey:(const std::string&)key {
    const auto it = GRID_REGIONS.find(key);
    std::string name = key;
    double carbon = 0.0;
    if (it != GRID_REGIONS.end()) {
        carbon = it->second.carbon_intensity;
        name = it->second.name;
    }
    if (english_) {
        if (key == "cn") name = "China";
        else if (key == "us") name = "United States Average";
        else if (key == "us_ca") name = "United States California";
        else if (key == "us_tx") name = "United States Texas";
        else if (key == "eu") name = "European Union Average";
        else if (key == "de") name = "Germany";
        else if (key == "fr") name = "France";
        else if (key == "no") name = "Norway";
        else if (key == "uk") name = "United Kingdom";
        else if (key == "jp") name = "Japan";
        else if (key == "au") name = "Australia";
        else if (key == "br") name = "Brazil";
        else if (key == "in") name = "India";
        else if (key == "global") name = "Global Average";
    }
    std::ostringstream ss;
    ss << name << " (" << (int)carbon << ")";
    return ns(ss.str());
}

- (void)reloadOptionTitles {
    NSInteger hw_index = hw_popup_.indexOfSelectedItem >= 0 ? hw_popup_.indexOfSelectedItem : 2;
    NSInteger grid_index = grid_popup_.indexOfSelectedItem >= 0 ? grid_popup_.indexOfSelectedItem : 13;

    [hw_popup_ removeAllItems];
    for (const auto& key : hw_keys_)
        [hw_popup_ addItemWithTitle:[self hardwareTitleForKey:key]];
    [hw_popup_ selectItemAtIndex:std::min<NSInteger>(hw_index, hw_popup_.numberOfItems - 1)];

    [grid_popup_ removeAllItems];
    for (const auto& key : grid_keys_)
        [grid_popup_ addItemWithTitle:[self gridTitleForKey:key]];
    [grid_popup_ selectItemAtIndex:std::min<NSInteger>(grid_index, grid_popup_.numberOfItems - 1)];
}

- (void)retranslate {
    window_.title = [self textCN:@"GreenComputing 碳排放静态分析器" en:@"GreenComputing Carbon Static Analyzer"];
    subtitle_.stringValue = [self textCN:@"碳排放静态分析器" en:@"Carbon Static Analyzer"];
    config_title_.stringValue = [self textCN:@"分析配置" en:@"Analysis Config"];
    language_title_.stringValue = [self textCN:@"语言" en:@"Language"];
    source_title_.stringValue = [self textCN:@"源路径" en:@"Source Path"];
    browse_button_.title = [self textCN:@"路径" en:@"Path"];
    hw_title_.stringValue = [self textCN:@"硬件配置" en:@"Hardware Profile"];
    grid_title_.stringValue = [self textCN:@"电网区域" en:@"Grid Region"];
    analyze_button_.title = [self textCN:@"开始分析" en:@"Run Analysis"];
    co2_title_.stringValue = [self textCN:@"总碳排放" en:@"Total Carbon"];
    energy_title_.stringValue = [self textCN:@"总能耗" en:@"Total Energy"];
    count_title_.stringValue = [self textCN:@"函数数" en:@"Functions"];

    [self reloadOptionTitles];
    chart_view_.english = english_;
    detail_view_.english = english_;
    if ([status_label_.stringValue isEqualToString:@"等待分析"] ||
        [status_label_.stringValue isEqualToString:@"Waiting for analysis"])
        status_label_.stringValue = [self textCN:@"等待分析" en:@"Waiting for analysis"];
    if (!program_.functions.empty()) {
        std::ostringstream status;
        status << (english_ ? "Analysis complete" : "分析完成");
        status << " · " << program_.analyzed_files
               << (english_ ? " files" : " 个文件");
        status << " · " << program_.functions.size()
               << (english_ ? " functions" : " 个函数");
        status_label_.stringValue = ns(status.str());
    }
    if (!detail_view_.program && detail_view_.selectedRow < 0)
        detail_view_.message = [self textCN:@"选择源文件或项目目录并点击“开始分析”。"
                                          en:@"Choose a source file or project folder and click Run Analysis."];
    [chart_view_ reloadData];
    [detail_view_ reloadData];
}

- (void)languageChanged:(id)sender {
    (void)sender;
    english_ = language_popup_.indexOfSelectedItem == 1;
    [self retranslate];
}

- (void)selectFile:(id)sender {
    (void)sender;

    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = YES;
    panel.allowsMultipleSelection = NO;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    panel.allowedFileTypes = @[
        @"c", @"h", @"cpp", @"cc", @"cxx", @"hpp", @"hh", @"hxx",
        @"java", @"js", @"mjs", @"cjs", @"jsx", @"ts", @"tsx",
        @"go", @"cs", @"rs"
    ];
#pragma clang diagnostic pop
    if ([panel runModal] == NSModalResponseOK) {
        source_path_ = panel.URL.path.UTF8String;
        file_field_.stringValue = ns(compact_input_path_label(source_path_));
    }
}

- (void)runAnalysis:(id)sender {
    (void)sender;

    std::string path = source_path_;
    if (path.empty())
        path = find_demo_path(app_search_hints());

    std::string resolved = resolve_existing_path(path, app_search_hints());
    if (!resolved.empty())
        path = resolved;

    if (path.empty() || !std::filesystem::exists(path)) {
        [self setError:((english_ ? "Source path not found: " : "找不到源路径: ") + path)];
        return;
    }
    source_path_ = path;
    file_field_.stringValue = ns(compact_input_path_label(source_path_));

    const auto hw_index = (size_t)hw_popup_.indexOfSelectedItem;
    const auto grid_index = (size_t)grid_popup_.indexOfSelectedItem;
    if (hw_index >= hw_keys_.size() || grid_index >= grid_keys_.size()) {
        [self setError:(english_ ? "Invalid configuration" : "配置无效")];
        return;
    }

    const std::string& hw_key = hw_keys_[hw_index];
    const std::string& grid_key = grid_keys_[grid_index];
    auto hw_it = HARDWARE_PROFILES.find(hw_key);
    auto grid_it = GRID_REGIONS.find(grid_key);
    if (hw_it == HARDWARE_PROFILES.end() || grid_it == GRID_REGIONS.end()) {
        [self setError:(english_ ? "Invalid configuration" : "配置无效")];
        return;
    }

    try {
        StaticAnalyzer analyzer;
        auto functions = analyzer.analyze_path(path);
        if (functions.empty()) {
            [self setError:(english_ ? "No function definitions were detected" : "没有识别到函数定义")];
            return;
        }

        const bool is_directory = std::filesystem::is_directory(std::filesystem::path(path));
        program_ = ProgramProfile{};
        program_.source_file = path;
        program_.language = is_directory
            ? StaticAnalyzer::summarize_languages(functions)
            : StaticAnalyzer::language_display_name(
                StaticAnalyzer::detect_language(path));
        program_.hardware_key = hw_key;
        program_.grid_key = grid_key;
        program_.analyzed_files = is_directory
            ? StaticAnalyzer::collect_supported_files(path).size()
            : 1;
        program_.source_is_directory = is_directory;
        program_.functions = std::move(functions);

        EnergyEstimator estimator(hw_it->second, grid_it->second);
        estimator.estimate_all(program_);
    } catch (const std::exception& e) {
        [self setError:e.what()];
        return;
    }

    std::ostringstream status;
    status << (english_ ? "Analysis complete" : "分析完成");
    status << " · " << program_.analyzed_files
           << (english_ ? " files" : " 个文件");
    status << " · " << program_.functions.size()
           << (english_ ? " functions" : " 个函数");
    status_label_.stringValue = ns(status.str());
    status_label_.textColor = gh_muted();
    co2_label_.stringValue = ns(fmt_co2(program_.total_co2_mg) + " CO2eq");
    energy_label_.stringValue = ns(fmt_energy(program_.total_joules));
    count_label_.stringValue = ns(std::to_string(program_.functions.size()));
    chart_view_.program = &program_;
    chart_view_.selectedRow = 0;
    [chart_view_ reloadData];
    [self showDetailForRow:0];
}

- (void)chartClicked:(CarbonChartView*)sender {
    [self showDetailForRow:sender.selectedRow];
}

- (void)setError:(const std::string&)message {
    status_label_.stringValue = ns(message);
    status_label_.textColor = [NSColor systemRedColor];
    detail_view_.program = nullptr;
    detail_view_.selectedRow = -1;
    detail_view_.message = ns(message);
    program_ = ProgramProfile{};
    co2_label_.stringValue = @"-";
    energy_label_.stringValue = @"-";
    count_label_.stringValue = @"-";
    chart_view_.program = &program_;
    chart_view_.selectedRow = -1;
    [chart_view_ reloadData];
    [detail_view_ reloadData];
}

- (void)showDetailForRow:(NSInteger)row {
    if (row < 0 || row >= (NSInteger)program_.functions.size()) {
        chart_view_.selectedRow = -1;
        [chart_view_ setNeedsDisplay:YES];
        detail_view_.program = nullptr;
        detail_view_.selectedRow = -1;
        detail_view_.message = [self textCN:@"选择折线图中的函数查看详情。" en:@"Select a function in the chart to show details."];
        [detail_view_ reloadData];
        return;
    }
    chart_view_.selectedRow = row;
    [chart_view_ setNeedsDisplay:YES];
    detail_view_.program = &program_;
    detail_view_.selectedRow = row;
    [detail_view_ reloadData];
}

@end

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    @autoreleasepool {
        NSApplication* app = [NSApplication sharedApplication];
        AppDelegate* delegate = [[AppDelegate alloc] init];
        app.delegate = delegate;
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        [app activateIgnoringOtherApps:YES];
        [app run];
    }
    return 0;
}
