#import <Cocoa/Cocoa.h>

#include <algorithm>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

#include "carbon_model.hpp"
#include "energy_estimator.hpp"
#include "function_profile.hpp"
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

static NSInteger top_hotspot_index(const ProgramProfile* program) {
    if (!program || program->functions.empty())
        return -1;

    NSInteger best = 0;
    for (NSInteger i = 1; i < (NSInteger)program->functions.size(); ++i) {
        if (program->functions[(size_t)i].estimated_co2_mg >
            program->functions[(size_t)best].estimated_co2_mg)
            best = i;
    }
    return best;
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

@interface CarbonChartView : NSView
@property(nonatomic, assign) ProgramProfile* program;
@property(nonatomic, assign) NSInteger selectedRow;
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
    NSInteger top_index = top_hotspot_index(_program);
    NSInteger count = _program ? (NSInteger)_program->functions.size() : 0;
    if (top_index >= 0)
        --count;
    CGFloat width = std::max<CGFloat>(820.0, 112.0 + std::max<NSInteger>(count - 1, 0) * 92.0);
    [self setFrameSize:NSMakeSize(width, 272.0)];
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

    [@"函数碳排放折线图" drawAtPoint:NSMakePoint(16, 14) withAttributes:title_attr];
    [@"最高函数单独统计，其余函数按字母顺序和非线性刻度绘制" drawAtPoint:NSMakePoint(150, 16)
                                                       withAttributes:muted_attr];

    if (!_program || _program->functions.empty()) {
        [@"完成分析后在这里显示每个函数的碳排放。" drawAtPoint:NSMakePoint(16, 58)
                                                withAttributes:muted_attr];
        return;
    }

    std::vector<NSInteger> plotted;
    NSInteger top_index = top_hotspot_index(_program);
    for (NSInteger i = 0; i < (NSInteger)_program->functions.size(); ++i) {
        if (i != top_index)
            plotted.push_back(i);
    }
    std::sort(plotted.begin(), plotted.end(), [&](NSInteger a, NSInteger b) {
        return _program->functions[(size_t)a].name < _program->functions[(size_t)b].name;
    });
    std::sort(plotted.begin(), plotted.end(), [&](NSInteger a, NSInteger b) {
        return _program->functions[(size_t)a].name < _program->functions[(size_t)b].name;
    });

    NSRect top_card = NSMakeRect(548, 10, 250, 38);
    if (top_index >= 0) {
        const auto& top_fp = _program->functions[(size_t)top_index];
        NSColor* top_fill = (_selectedRow == top_index) ? gh_card_subtle() : gh_bg();
        [top_fill setFill];
        NSBezierPath* box = [NSBezierPath bezierPathWithRoundedRect:top_card xRadius:8 yRadius:8];
        [box fill];
        [gh_border() setStroke];
        [box setLineWidth:1.0];
        [box stroke];
        [ns("最高: " + shorten(top_fp.name, 14))
            drawAtPoint:NSMakePoint(top_card.origin.x + 12, top_card.origin.y + 6)
         withAttributes:muted_attr];
        [ns(fmt_co2(top_fp.estimated_co2_mg) + " CO2eq")
            drawAtPoint:NSMakePoint(top_card.origin.x + 138, top_card.origin.y + 6)
         withAttributes:value_attr];
    }

    if (plotted.empty()) {
        [@"除最高函数外没有可绘制的函数。" drawAtPoint:NSMakePoint(16, 58)
                                            withAttributes:muted_attr];
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
    const CGFloat top = 52.0;
    const CGFloat plot_h = 126.0;
    const CGFloat step = 92.0;
    const CGFloat axis_y = top + plot_h;

    if (_selectedRow >= 0) {
        auto it = std::find(plotted.begin(), plotted.end(), _selectedRow);
        if (it != plotted.end()) {
            CGFloat x = left + (it - plotted.begin()) * step;
            [gh_card_subtle() setFill];
            NSBezierPath* selected = [NSBezierPath bezierPathWithRoundedRect:NSMakeRect(x - 42, 42, 84, 232)
                                                                     xRadius:8
                                                                     yRadius:8];
            [selected fill];
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
            [gh_text() setStroke];
            NSBezierPath* ring = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(x - 8, y - 8, 16, 16)];
            [ring setLineWidth:2.0];
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

    NSInteger top_index = top_hotspot_index(_program);
    std::vector<NSInteger> plotted;
    for (NSInteger i = 0; i < (NSInteger)_program->functions.size(); ++i) {
        if (i != top_index)
            plotted.push_back(i);
    }

    NSRect top_card = NSMakeRect(548, 10, 250, 38);
    if (top_index >= 0 && NSPointInRect(p, top_card)) {
        _selectedRow = top_index;
        [self setNeedsDisplay:YES];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Warc-performSelector-leaks"
        if (_target && _action && [_target respondsToSelector:_action])
            [_target performSelector:_action withObject:self];
#pragma clang diagnostic pop
        return;
    }

    const CGFloat left = 58.0;
    const CGFloat step = 92.0;
    NSInteger row = (NSInteger)std::llround((p.x - left) / step);
    if (row >= 0 && row < (NSInteger)plotted.size()) {
        _selectedRow = plotted[(size_t)row];
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
@property(nonatomic, copy) NSString* message;
- (void)reloadData;
@end

@implementation CarbonDetailView

- (instancetype)initWithFrame:(NSRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        _selectedRow = -1;
        _message = @"选择源文件并点击“开始分析”。";
        self.wantsLayer = YES;
        self.layer.backgroundColor = gh_card().CGColor;
    }
    return self;
}

- (BOOL)isFlipped {
    return YES;
}

- (void)reloadData {
    CGFloat height = 250.0;
    if (_program && _selectedRow >= 0 && _selectedRow < (NSInteger)_program->functions.size()) {
        const auto& fp = _program->functions[(size_t)_selectedRow];
        height = 500.0;
        if (!fp.callees.empty())
            height += 72.0 + std::min<size_t>(fp.callees.size(), 8) * 46.0;
        if (!fp.warnings.empty() || !fp.suggestions.empty())
            height += 52.0 + std::min<size_t>(fp.warnings.size() + fp.suggestions.size(), 6) * 22.0;
    }
    [self setFrameSize:NSMakeSize(std::max<CGFloat>(820.0, self.frame.size.width), height)];
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
        NSString* msg = _message ? _message : @"选择函数后显示详情。";
        [msg drawAtPoint:NSMakePoint(18, 18) withAttributes:muted_attr];
        return;
    }

    const auto& fp = _program->functions[(size_t)_selectedRow];
    [@"函数详情" drawAtPoint:NSMakePoint(18, 16) withAttributes:title_attr];
    [ns(fp.name) drawAtPoint:NSMakePoint(18, 42)
              withAttributes:@{
                  NSFontAttributeName: [NSFont systemFontOfSize:18.0 weight:NSFontWeightSemibold],
                  NSForegroundColorAttributeName: gh_blue()
              }];

    std::ostringstream location;
    location << fp.file << ":" << fp.line_start << "-" << fp.line_end;
    [ns(shorten(location.str(), 96)) drawAtPoint:NSMakePoint(18, 70) withAttributes:muted_attr];

    [self drawMetric:@"碳排放"
               value:ns(fmt_co2(fp.estimated_co2_mg) + " CO2eq")
               frame:NSMakeRect(18, 102, 180, 66)
               color:gh_green()];
    [self drawMetric:@"能耗"
               value:ns(fmt_energy(fp.estimated_joules))
               frame:NSMakeRect(214, 102, 150, 66)
               color:gh_blue()];
    [self drawMetric:@"能耗评分"
               value:ns(std::to_string((long long)fp.energy_score))
               frame:NSMakeRect(380, 102, 150, 66)
               color:gh_text()];
    std::ostringstream loop_text;
    loop_text << "深度 " << fp.loops.depth << " / " << fp.loops.count << " 个";
    [self drawMetric:@"循环"
               value:ns(loop_text.str())
               frame:NSMakeRect(546, 102, 150, 66)
               color:fp.loops.depth >= 2 ? gh_orange() : gh_text()];

    struct Part {
        const char* label;
        uint64_t count;
        double weight;
        NSColor* color;
    };
    const std::vector<Part> parts = {
        {"ALU", fp.raw.alu, weight_of(InstrCat::ALU), gh_blue()},
        {"浮点", fp.raw.fpu, weight_of(InstrCat::FPU), gh_orange()},
        {"内存", fp.raw.memory, weight_of(InstrCat::MEMORY), gh_red()},
        {"分支", fp.raw.branch, weight_of(InstrCat::BRANCH), gh_muted()},
        {"IO", fp.raw.io, weight_of(InstrCat::IO), gh_red()},
        {"SIMD", fp.raw.simd, weight_of(InstrCat::SIMD), gh_green()},
        {"同步", fp.raw.atomic, weight_of(InstrCat::ATOMIC), gh_orange()},
    };

    double total = 0.0;
    for (const auto& part : parts)
        total += part.count * part.weight;
    if (total <= 0.0)
        total = 1.0;

    [@"指令贡献拆分" drawAtPoint:NSMakePoint(18, 194) withAttributes:title_attr];
    [@"按类别权重估算，不包含真实运行采样" drawAtPoint:NSMakePoint(122, 196) withAttributes:muted_attr];

    CGFloat stack_x = 18.0;
    CGFloat stack_y = 226.0;
    CGFloat stack_w = 678.0;
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

    CGFloat chip_w = 214.0;
    CGFloat chip_h = 74.0;
    CGFloat chip_gap = 18.0;
    for (size_t i = 0; i < parts.size(); ++i) {
        const auto& part = parts[i];
        double weighted = part.count * part.weight;
        double share = weighted / total;
        double co2 = fp.estimated_co2_mg * share;
        CGFloat x = 18.0 + (CGFloat)(i % 3) * (chip_w + chip_gap);
        CGFloat y = 258.0 + (CGFloat)(i / 3) * 84.0;

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

    CGFloat section_y = 526.0;
    if (!fp.callees.empty()) {
        [@"调用分布" drawAtPoint:NSMakePoint(18, section_y) withAttributes:title_attr];
        [@"被调用函数的估算碳排放，不计入当前函数自身指令拆分" drawAtPoint:NSMakePoint(86, section_y + 2)
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
            CGFloat row_w = 668.0;
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

            CGFloat spark_x = row_x + 250;
            CGFloat spark_w = 210.0;
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

            std::string value = call.known ? fmt_co2(call.co2) + " CO2eq" : "未估算";
            [ns(value) drawAtPoint:NSMakePoint(row_x + 482, section_y + 10)
                    withAttributes:mono_attr];

            section_y += 46.0;
            if (++shown >= 8)
                break;
        }
        if (calls.size() > 8)
            [ns("还有 " + std::to_string(calls.size() - 8) + " 个调用未显示")
                drawAtPoint:NSMakePoint(28, section_y)
             withAttributes:muted_attr];
        section_y += 22.0;
    }

    if (!fp.warnings.empty() || !fp.suggestions.empty()) {
        [@"提示" drawAtPoint:NSMakePoint(18, section_y) withAttributes:title_attr];
        section_y += 28.0;
        int shown = 0;
        for (const auto& item : fp.warnings) {
            [ns("警告: " + item) drawAtPoint:NSMakePoint(28, section_y)
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
            [ns("建议: " + item) drawAtPoint:NSMakePoint(28, section_y)
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

@interface AppDelegate : NSObject <NSApplicationDelegate>
@end

@implementation AppDelegate {
    NSWindow* window_;
    NSTextField* file_field_;
    NSPopUpButton* hw_popup_;
    NSPopUpButton* grid_popup_;
    NSTextField* status_label_;
    NSTextField* co2_label_;
    NSTextField* energy_label_;
    NSTextField* count_label_;
    CarbonChartView* chart_view_;
    CarbonDetailView* detail_view_;

    std::vector<std::string> hw_keys_;
    std::vector<std::string> grid_keys_;
    ProgramProfile program_;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        hw_keys_ = {
            "rpi4", "laptop_low", "laptop_mid", "desktop_mid",
            "desktop_high", "server_1u", "server_hpc"
        };
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
    [window_ center];

    NSView* root = window_.contentView;
    root.wantsLayer = YES;
    root.layer.backgroundColor = gh_bg().CGColor;

    NSTextField* title = label(NSMakeRect(24, 714, 280, 28), @"GreenComputing", 22.0);
    title.font = [NSFont systemFontOfSize:22.0 weight:NSFontWeightSemibold];
    title.textColor = gh_text();
    [root addSubview:title];
    NSTextField* subtitle = label(NSMakeRect(216, 718, 220, 20), @"碳排放静态分析器", 13.0);
    subtitle.textColor = gh_muted();
    [root addSubview:subtitle];

    NSView* side = [[NSView alloc] initWithFrame:NSMakeRect(24, 24, 292, 672)];
    side.wantsLayer = YES;
    side.layer.backgroundColor = gh_card().CGColor;
    side.layer.borderColor = gh_border().CGColor;
    side.layer.borderWidth = 1.0;
    side.layer.cornerRadius = 8.0;
    [root addSubview:side];

    NSTextField* config_title = label(NSMakeRect(44, 654, 120, 22), @"分析配置", 15.0);
    config_title.font = [NSFont systemFontOfSize:15.0 weight:NSFontWeightSemibold];
    config_title.textColor = gh_text();
    [root addSubview:config_title];

    [root addSubview:label(NSMakeRect(44, 604, 120, 20), @"源文件", 13.0)];
    file_field_ = [[NSTextField alloc] initWithFrame:NSMakeRect(44, 574, 188, 28)];
    file_field_.stringValue = @"demo.cpp";
    file_field_.textColor = gh_text();
    file_field_.backgroundColor = gh_bg();
    [root addSubview:file_field_];

    NSButton* browse = [[NSButton alloc] initWithFrame:NSMakeRect(238, 574, 58, 28)];
    browse.title = @"选择";
    browse.bezelStyle = NSBezelStyleRounded;
    browse.target = self;
    browse.action = @selector(selectFile:);
    [root addSubview:browse];

    [root addSubview:label(NSMakeRect(44, 526, 120, 20), @"硬件配置", 13.0)];
    hw_popup_ = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(44, 494, 252, 30)];
    for (const auto& key : hw_keys_) {
        const auto it = HARDWARE_PROFILES.find(key);
        [hw_popup_ addItemWithTitle:ns(it != HARDWARE_PROFILES.end() ? it->second.name : key)];
    }
    [hw_popup_ selectItemAtIndex:2];
    [root addSubview:hw_popup_];

    [root addSubview:label(NSMakeRect(44, 446, 120, 20), @"电网区域", 13.0)];
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

    NSButton* analyze = [[NSButton alloc] initWithFrame:NSMakeRect(44, 354, 252, 34)];
    analyze.title = @"开始分析";
    analyze.bezelStyle = NSBezelStyleRounded;
    analyze.contentTintColor = gh_blue();
    analyze.target = self;
    analyze.action = @selector(runAnalysis:);
    [root addSubview:analyze];

    status_label_ = label(NSMakeRect(44, 306, 252, 40), @"等待分析", 13.0);
    status_label_.textColor = gh_muted();
    status_label_.lineBreakMode = NSLineBreakByWordWrapping;
    [root addSubview:status_label_];

    NSView* summary = [[NSView alloc] initWithFrame:NSMakeRect(336, 616, 820, 80)];
    summary.wantsLayer = YES;
    summary.layer.backgroundColor = gh_card().CGColor;
    summary.layer.borderColor = gh_border().CGColor;
    summary.layer.borderWidth = 1.0;
    summary.layer.cornerRadius = 8.0;
    [root addSubview:summary];

    [root addSubview:label(NSMakeRect(362, 668, 120, 18), @"总碳排放", 12.0)];
    co2_label_ = value_label(NSMakeRect(362, 640, 190, 24));
    [root addSubview:co2_label_];

    [root addSubview:label(NSMakeRect(604, 668, 120, 18), @"总能耗", 12.0)];
    energy_label_ = value_label(NSMakeRect(604, 640, 190, 24));
    [root addSubview:energy_label_];

    [root addSubview:label(NSMakeRect(846, 668, 120, 18), @"函数数", 12.0)];
    count_label_ = value_label(NSMakeRect(846, 640, 190, 24));
    [root addSubview:count_label_];

    NSScrollView* chart_scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(336, 324, 820, 272)];
    chart_scroll.hasVerticalScroller = YES;
    chart_scroll.hasHorizontalScroller = YES;
    chart_scroll.borderType = NSNoBorder;
    chart_scroll.wantsLayer = YES;
    chart_scroll.layer.backgroundColor = gh_card().CGColor;
    chart_scroll.layer.borderColor = gh_border().CGColor;
    chart_scroll.layer.borderWidth = 1.0;
    chart_scroll.layer.cornerRadius = 8.0;
    chart_view_ = [[CarbonChartView alloc] initWithFrame:NSMakeRect(0, 0, 820, 272)];
    chart_view_.target = self;
    chart_view_.action = @selector(chartClicked:);
    chart_scroll.documentView = chart_view_;
    [root addSubview:chart_scroll];

    NSScrollView* detail_scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(336, 24, 820, 280)];
    detail_scroll.hasVerticalScroller = YES;
    detail_scroll.borderType = NSNoBorder;
    detail_scroll.wantsLayer = YES;
    detail_scroll.layer.backgroundColor = gh_card().CGColor;
    detail_scroll.layer.borderColor = gh_border().CGColor;
    detail_scroll.layer.borderWidth = 1.0;
    detail_scroll.layer.cornerRadius = 8.0;
    detail_view_ = [[CarbonDetailView alloc] initWithFrame:NSMakeRect(0, 0, 820, 250)];
    detail_scroll.documentView = detail_view_;
    [root addSubview:detail_scroll];

    [window_ makeKeyAndOrderFront:nil];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
    (void)sender;
    return YES;
}

- (void)selectFile:(id)sender {
    (void)sender;

    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = NO;
    panel.allowsMultipleSelection = NO;
    if ([panel runModal] == NSModalResponseOK)
        file_field_.stringValue = panel.URL.path;
}

- (void)runAnalysis:(id)sender {
    (void)sender;

    std::string path = file_field_.stringValue.UTF8String;
    if (path.empty())
        path = "demo.cpp";

    if (!std::filesystem::exists(path)) {
        std::string parent = "../" + path;
        if (std::filesystem::exists(parent))
            path = parent;
    }

    if (!std::filesystem::exists(path)) {
        [self setError:("找不到源文件: " + path)];
        return;
    }

    const auto hw_index = (size_t)hw_popup_.indexOfSelectedItem;
    const auto grid_index = (size_t)grid_popup_.indexOfSelectedItem;
    if (hw_index >= hw_keys_.size() || grid_index >= grid_keys_.size()) {
        [self setError:"配置无效"];
        return;
    }

    const std::string& hw_key = hw_keys_[hw_index];
    const std::string& grid_key = grid_keys_[grid_index];
    auto hw_it = HARDWARE_PROFILES.find(hw_key);
    auto grid_it = GRID_REGIONS.find(grid_key);
    if (hw_it == HARDWARE_PROFILES.end() || grid_it == GRID_REGIONS.end()) {
        [self setError:"配置无效"];
        return;
    }

    try {
        StaticAnalyzer analyzer;
        auto functions = analyzer.analyze(path);
        if (functions.empty()) {
            [self setError:"没有识别到函数定义"];
            return;
        }

        program_ = ProgramProfile{};
        program_.source_file = path;
        program_.hardware_key = hw_key;
        program_.grid_key = grid_key;
        program_.functions = std::move(functions);

        EnergyEstimator estimator(hw_it->second, grid_it->second);
        estimator.estimate_all(program_);
    } catch (const std::exception& e) {
        [self setError:e.what()];
        return;
    }

    status_label_.stringValue = @"分析完成";
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
        detail_view_.program = nullptr;
        detail_view_.selectedRow = -1;
        detail_view_.message = @"选择折线图中的函数查看详情。";
        [detail_view_ reloadData];
        return;
    }
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
