#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QVBoxLayout>

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "carbon_model.hpp"
#include "energy_estimator.hpp"
#include "function_profile.hpp"
#include "static_analyzer.hpp"

static QString qs(const std::string& text) {
    return QString::fromStdString(text);
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
    return ss.str() + " CO2eq";
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

class MainWindow : public QMainWindow {
public:
    MainWindow() {
        hw_keys_ = {
            "rpi4", "laptop_low", "laptop_mid", "desktop_mid",
            "desktop_high", "server_1u", "server_hpc"
        };
        grid_keys_ = {
            "cn", "us", "us_ca", "us_tx", "eu", "de", "fr",
            "no", "uk", "jp", "au", "br", "in", "global"
        };

        setWindowTitle("GreenComputing");
        resize(1180, 760);
        setMinimumSize(920, 620);

        auto* central = new QWidget(this);
        auto* root = new QVBoxLayout(central);
        root->setContentsMargins(18, 18, 18, 18);
        root->setSpacing(14);

        title_ = new QLabel("GreenComputing");
        title_->setObjectName("Title");
        root->addWidget(title_);

        auto* splitter = new QSplitter(Qt::Horizontal);
        root->addWidget(splitter, 1);

        config_box_ = new QGroupBox;
        auto* config = new QVBoxLayout(config_box_);
        config->setSpacing(12);

        file_edit_ = new QLineEdit("demo.cpp");
        browse_button_ = new QPushButton;
        connect(browse_button_, &QPushButton::clicked, this, [this]() {
            QString path = QFileDialog::getOpenFileName(this, english_ ? "Select C++ Source File" : "选择 C++ 源文件", QString(),
                                                        "C++ Source (*.cpp *.cc *.cxx *.hpp *.h);;All Files (*)");
            if (!path.isEmpty())
                file_edit_->setText(path);
        });

        language_combo_ = new QComboBox;
        language_combo_->addItem("中文");
        language_combo_->addItem("English");
        connect(language_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
            english_ = (index == 1);
            retranslate();
        });

        hw_combo_ = new QComboBox;
        hw_combo_->setCurrentIndex(2);

        grid_combo_ = new QComboBox;
        grid_combo_->setCurrentIndex(13);

        run_button_ = new QPushButton;
        run_button_->setDefault(true);
        connect(run_button_, &QPushButton::clicked, this, [this]() { runAnalysis(); });

        status_ = new QLabel;
        status_->setWordWrap(true);

        auto* form = new QFormLayout;
        language_label_ = new QLabel;
        source_label_ = new QLabel;
        hw_label_ = new QLabel;
        grid_label_ = new QLabel;
        form->addRow(language_label_, language_combo_);
        form->addRow(source_label_, file_edit_);
        form->addRow("", browse_button_);
        form->addRow(hw_label_, hw_combo_);
        form->addRow(grid_label_, grid_combo_);
        config->addLayout(form);
        config->addWidget(run_button_);
        config->addWidget(status_);
        config->addStretch(1);
        splitter->addWidget(config_box_);

        auto* workspace = new QWidget;
        auto* workspace_layout = new QVBoxLayout(workspace);
        workspace_layout->setContentsMargins(0, 0, 0, 0);
        workspace_layout->setSpacing(12);

        summary_box_ = new QGroupBox;
        auto* summary_layout = new QGridLayout(summary_box_);
        co2_title_ = new QLabel;
        energy_title_ = new QLabel;
        count_title_ = new QLabel;
        co2_ = new QLabel("-");
        energy_ = new QLabel("-");
        count_ = new QLabel("-");
        summary_layout->addWidget(co2_title_, 0, 0);
        summary_layout->addWidget(energy_title_, 0, 1);
        summary_layout->addWidget(count_title_, 0, 2);
        summary_layout->addWidget(co2_, 1, 0);
        summary_layout->addWidget(energy_, 1, 1);
        summary_layout->addWidget(count_, 1, 2);
        workspace_layout->addWidget(summary_box_);

        table_ = new QTableWidget(0, 6);
        table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        table_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
        table_->verticalHeader()->setVisible(false);
        table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        table_->setSelectionMode(QAbstractItemView::SingleSelection);
        connect(table_, &QTableWidget::currentCellChanged, this,
                [this](int row, int, int, int) { showDetail(row); });
        workspace_layout->addWidget(table_, 2);

        detail_ = new QTextEdit;
        detail_->setReadOnly(true);
        workspace_layout->addWidget(detail_, 1);

        splitter->addWidget(workspace);
        splitter->setStretchFactor(0, 0);
        splitter->setStretchFactor(1, 1);
        splitter->setSizes({300, 850});
        setCentralWidget(central);

        setStyleSheet(R"(
            QMainWindow, QWidget {
                background: #0d1117;
                color: #c9d1d9;
                font-size: 13px;
            }
            QLabel#Title {
                font-size: 24px;
                font-weight: 700;
                color: #f0f6fc;
            }
            QGroupBox {
                border: 1px solid #30363d;
                border-radius: 8px;
                margin-top: 10px;
                padding: 12px;
                background: #161b22;
            }
            QGroupBox::title {
                subcontrol-origin: margin;
                left: 12px;
                padding: 0 4px;
                color: #8b949e;
            }
            QLineEdit, QComboBox, QTextEdit, QTableWidget {
                background: #0d1117;
                color: #c9d1d9;
                border: 1px solid #30363d;
                border-radius: 6px;
                padding: 6px;
            }
            QPushButton {
                background: #238636;
                color: #ffffff;
                border: 1px solid #2ea043;
                border-radius: 6px;
                padding: 8px 12px;
            }
            QPushButton:hover { background: #2ea043; }
            QHeaderView::section {
                background: #21262d;
                color: #c9d1d9;
                border: 0;
                padding: 6px;
            }
            QTableWidget::item:selected { background: #1f6feb; }
        )");
        retranslate();
        hw_combo_->setCurrentIndex(2);
        grid_combo_->setCurrentIndex(13);
    }

private:
    QString trText(const char* zh, const char* en) const {
        return english_ ? QString::fromUtf8(en) : QString::fromUtf8(zh);
    }

    QString hwTitle(const std::string& key) const {
        if (!english_)
            return qs(HARDWARE_PROFILES.at(key).name);
        if (key == "rpi4") return "Raspberry Pi 4";
        if (key == "laptop_low") return "Laptop Low Power (~15W TDP)";
        if (key == "laptop_mid") return "Laptop Mid Range (~28W TDP)";
        if (key == "desktop_mid") return "Desktop Mid Range (~65W TDP)";
        if (key == "desktop_high") return "Desktop High End (~125W TDP)";
        if (key == "server_1u") return "Server 1U";
        if (key == "server_hpc") return "Server HPC Node";
        return qs(key);
    }

    QString gridTitle(const std::string& key) const {
        const auto it = GRID_REGIONS.find(key);
        const int carbon = it != GRID_REGIONS.end() ? (int)it->second.carbon_intensity : 0;
        std::string name = key;
        if (!english_ && it != GRID_REGIONS.end()) {
            name = it->second.name;
        } else if (english_) {
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
        ss << name << " (" << carbon << ")";
        return qs(ss.str());
    }

    void rebuildOptionLabels() {
        const int hw_index = std::max(0, hw_combo_->currentIndex());
        const int grid_index = std::max(0, grid_combo_->currentIndex());

        hw_combo_->blockSignals(true);
        hw_combo_->clear();
        for (const auto& key : hw_keys_)
            hw_combo_->addItem(hwTitle(key));
        hw_combo_->setCurrentIndex(std::min(hw_index, hw_combo_->count() - 1));
        hw_combo_->blockSignals(false);

        grid_combo_->blockSignals(true);
        grid_combo_->clear();
        for (const auto& key : grid_keys_)
            grid_combo_->addItem(gridTitle(key));
        grid_combo_->setCurrentIndex(std::min(grid_index, grid_combo_->count() - 1));
        grid_combo_->blockSignals(false);
    }

    void retranslate() {
        setWindowTitle(english_ ? "GreenComputing Carbon Static Analyzer" : "GreenComputing 碳排放静态分析器");
        config_box_->setTitle(trText("分析配置", "Analysis Configuration"));
        summary_box_->setTitle(trText("总体结果", "Summary"));
        language_label_->setText(trText("语言", "Language"));
        source_label_->setText(trText("源文件", "Source File"));
        hw_label_->setText(trText("硬件配置", "Hardware Profile"));
        grid_label_->setText(trText("电网区域", "Grid Region"));
        browse_button_->setText(trText("选择源文件", "Choose Source File"));
        run_button_->setText(trText("开始分析", "Run Analysis"));
        co2_title_->setText(trText("总碳排放", "Total Carbon"));
        energy_title_->setText(trText("总能耗", "Total Energy"));
        count_title_->setText(trText("函数数", "Functions"));
        table_->setHorizontalHeaderLabels({
            trText("函数", "Function"),
            trText("碳排放", "Carbon"),
            trText("能耗", "Energy"),
            trText("评分", "Score"),
            trText("循环", "Loops"),
            trText("位置", "Location")
        });
        detail_->setPlaceholderText(trText("选择函数后显示详情。", "Select a function to show details."));
        rebuildOptionLabels();
        if (program_.functions.empty() && status_->text().isEmpty())
            status_->setText(trText("等待分析", "Waiting for analysis"));
        else if (status_->text() == "等待分析" || status_->text() == "Waiting for analysis")
            status_->setText(trText("等待分析", "Waiting for analysis"));
        else if (status_->text() == "分析完成" || status_->text() == "Analysis complete")
            status_->setText(trText("分析完成", "Analysis complete"));
        if (!program_.functions.empty()) {
            fillTable();
            showDetail(table_->currentRow());
        }
    }

    void runAnalysis() {
        std::string path = file_edit_->text().toStdString();
        if (path.empty())
            path = "demo.cpp";
        if (!std::filesystem::exists(path)) {
            std::string parent = "../" + path;
            if (std::filesystem::exists(parent))
                path = parent;
        }
        if (!std::filesystem::exists(path)) {
            setError((english_ ? "Source file not found: " : "找不到源文件: ") + path);
            return;
        }

        const int hw_index = hw_combo_->currentIndex();
        const int grid_index = grid_combo_->currentIndex();
        if (hw_index < 0 || grid_index < 0 ||
            hw_index >= (int)hw_keys_.size() || grid_index >= (int)grid_keys_.size()) {
            setError(english_ ? "Invalid configuration" : "配置无效");
            return;
        }

        const std::string& hw_key = hw_keys_[(size_t)hw_index];
        const std::string& grid_key = grid_keys_[(size_t)grid_index];
        auto hw_it = HARDWARE_PROFILES.find(hw_key);
        auto grid_it = GRID_REGIONS.find(grid_key);
        if (hw_it == HARDWARE_PROFILES.end() || grid_it == GRID_REGIONS.end()) {
            setError(english_ ? "Invalid configuration" : "配置无效");
            return;
        }

        try {
            StaticAnalyzer analyzer;
            auto functions = analyzer.analyze(path);
            if (functions.empty()) {
                setError(english_ ? "No function definitions were detected" : "没有识别到函数定义");
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
            setError(e.what());
            return;
        }

        status_->setText(trText("分析完成", "Analysis complete"));
        co2_->setText(qs(fmt_co2(program_.total_co2_mg)));
        energy_->setText(qs(fmt_energy(program_.total_joules)));
        count_->setText(QString::number((int)program_.functions.size()));
        fillTable();
    }

    void fillTable() {
        table_->setRowCount((int)program_.functions.size());
        for (int row = 0; row < (int)program_.functions.size(); ++row) {
            const auto& fp = program_.functions[(size_t)row];
            std::ostringstream location;
            location << fp.file << ":" << fp.line_start << "-" << fp.line_end;
            std::ostringstream loop;
            loop << fp.loops.depth << " / " << fp.loops.count;

            table_->setItem(row, 0, new QTableWidgetItem(qs(fp.name)));
            table_->setItem(row, 1, new QTableWidgetItem(qs(fmt_co2(fp.estimated_co2_mg))));
            table_->setItem(row, 2, new QTableWidgetItem(qs(fmt_energy(fp.estimated_joules))));
            table_->setItem(row, 3, new QTableWidgetItem(QString::number(fp.energy_score, 'f', 0)));
            table_->setItem(row, 4, new QTableWidgetItem(qs(loop.str())));
            table_->setItem(row, 5, new QTableWidgetItem(qs(location.str())));
        }
        if (!program_.functions.empty())
            table_->selectRow(0);
    }

    void showDetail(int row) {
        if (row < 0 || row >= (int)program_.functions.size()) {
            detail_->clear();
            return;
        }

        const auto& fp = program_.functions[(size_t)row];
        std::ostringstream out;
        out << fp.name << "\n";
        out << fp.file << ":" << fp.line_start << "-" << fp.line_end << "\n\n";
        out << (english_ ? "Carbon: " : "碳排放: ") << fmt_co2(fp.estimated_co2_mg) << "\n";
        out << (english_ ? "Energy: " : "能耗: ") << fmt_energy(fp.estimated_joules) << "\n";
        out << (english_ ? "Energy score: " : "能耗评分: ") << std::fixed << std::setprecision(0) << fp.energy_score << "\n";
        out << (english_ ? "Loops: depth " : "循环: 深度 ") << fp.loops.depth
            << (english_ ? ", count " : ", 数量 ") << fp.loops.count << "\n\n";
        out << (english_ ? "Instruction Counts\n" : "指令计数\n");
        out << "ALU: " << fp.raw.alu << "\n";
        out << (english_ ? "Floating point: " : "浮点: ") << fp.raw.fpu << "\n";
        out << (english_ ? "Memory: " : "内存: ") << fp.raw.memory << "\n";
        out << (english_ ? "Branch: " : "分支: ") << fp.raw.branch << "\n";
        out << "IO: " << fp.raw.io << "\n";
        out << "SIMD: " << fp.raw.simd << "\n";
        out << (english_ ? "Synchronization: " : "同步: ") << fp.raw.atomic << "\n";

        if (!fp.callees.empty()) {
            out << (english_ ? "\nCalls\n" : "\n调用\n");
            for (const auto& callee : fp.callees)
                out << "- " << callee << "\n";
        }
        if (!fp.warnings.empty()) {
            out << (english_ ? "\nWarnings\n" : "\n警告\n");
            for (const auto& warning : fp.warnings)
                out << "- " << warning << "\n";
        }
        if (!fp.suggestions.empty()) {
            out << (english_ ? "\nSuggestions\n" : "\n建议\n");
            for (const auto& suggestion : fp.suggestions)
                out << "- " << suggestion << "\n";
        }

        detail_->setText(qs(out.str()));
    }

    void setError(const std::string& message) {
        status_->setText(qs(message));
        co2_->setText("-");
        energy_->setText("-");
        count_->setText("-");
        table_->setRowCount(0);
        detail_->setText(qs(message));
        QMessageBox::warning(this, "GreenComputing", qs(message));
    }

    bool english_ = false;
    QLabel* title_ = nullptr;
    QGroupBox* config_box_ = nullptr;
    QGroupBox* summary_box_ = nullptr;
    QLabel* language_label_ = nullptr;
    QLabel* source_label_ = nullptr;
    QLabel* hw_label_ = nullptr;
    QLabel* grid_label_ = nullptr;
    QComboBox* language_combo_ = nullptr;
    QPushButton* browse_button_ = nullptr;
    QPushButton* run_button_ = nullptr;
    QLineEdit* file_edit_ = nullptr;
    QComboBox* hw_combo_ = nullptr;
    QComboBox* grid_combo_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* co2_title_ = nullptr;
    QLabel* energy_title_ = nullptr;
    QLabel* count_title_ = nullptr;
    QLabel* co2_ = nullptr;
    QLabel* energy_ = nullptr;
    QLabel* count_ = nullptr;
    QTableWidget* table_ = nullptr;
    QTextEdit* detail_ = nullptr;

    std::vector<std::string> hw_keys_;
    std::vector<std::string> grid_keys_;
    ProgramProfile program_;
};

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    MainWindow window;
    window.show();
    return app.exec();
}
