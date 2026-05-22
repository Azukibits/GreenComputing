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

        auto* title = new QLabel("GreenComputing");
        title->setObjectName("Title");
        root->addWidget(title);

        auto* splitter = new QSplitter(Qt::Horizontal);
        root->addWidget(splitter, 1);

        auto* config_box = new QGroupBox("分析配置");
        auto* config = new QVBoxLayout(config_box);
        config->setSpacing(12);

        file_edit_ = new QLineEdit("demo.cpp");
        auto* browse = new QPushButton("选择源文件");
        connect(browse, &QPushButton::clicked, this, [this]() {
            QString path = QFileDialog::getOpenFileName(this, "选择 C++ 源文件", QString(),
                                                        "C++ Source (*.cpp *.cc *.cxx *.hpp *.h);;All Files (*)");
            if (!path.isEmpty())
                file_edit_->setText(path);
        });

        hw_combo_ = new QComboBox;
        for (const auto& key : hw_keys_) {
            const auto it = HARDWARE_PROFILES.find(key);
            hw_combo_->addItem(qs(it != HARDWARE_PROFILES.end() ? it->second.name : key));
        }
        hw_combo_->setCurrentIndex(2);

        grid_combo_ = new QComboBox;
        for (const auto& key : grid_keys_) {
            const auto it = GRID_REGIONS.find(key);
            if (it != GRID_REGIONS.end()) {
                std::ostringstream ss;
                ss << it->second.name << " (" << (int)it->second.carbon_intensity << ")";
                grid_combo_->addItem(qs(ss.str()));
            } else {
                grid_combo_->addItem(qs(key));
            }
        }
        grid_combo_->setCurrentIndex(13);

        auto* run = new QPushButton("开始分析");
        run->setDefault(true);
        connect(run, &QPushButton::clicked, this, [this]() { runAnalysis(); });

        status_ = new QLabel("等待分析");
        status_->setWordWrap(true);

        auto* form = new QFormLayout;
        form->addRow("源文件", file_edit_);
        form->addRow("", browse);
        form->addRow("硬件配置", hw_combo_);
        form->addRow("电网区域", grid_combo_);
        config->addLayout(form);
        config->addWidget(run);
        config->addWidget(status_);
        config->addStretch(1);
        splitter->addWidget(config_box);

        auto* workspace = new QWidget;
        auto* workspace_layout = new QVBoxLayout(workspace);
        workspace_layout->setContentsMargins(0, 0, 0, 0);
        workspace_layout->setSpacing(12);

        auto* summary = new QGroupBox("总体结果");
        auto* summary_layout = new QGridLayout(summary);
        co2_ = new QLabel("-");
        energy_ = new QLabel("-");
        count_ = new QLabel("-");
        summary_layout->addWidget(new QLabel("总碳排放"), 0, 0);
        summary_layout->addWidget(new QLabel("总能耗"), 0, 1);
        summary_layout->addWidget(new QLabel("函数数"), 0, 2);
        summary_layout->addWidget(co2_, 1, 0);
        summary_layout->addWidget(energy_, 1, 1);
        summary_layout->addWidget(count_, 1, 2);
        workspace_layout->addWidget(summary);

        table_ = new QTableWidget(0, 6);
        table_->setHorizontalHeaderLabels({"函数", "碳排放", "能耗", "评分", "循环", "位置"});
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
        detail_->setPlaceholderText("选择函数后显示详情。");
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
    }

private:
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
            setError("找不到源文件: " + path);
            return;
        }

        const int hw_index = hw_combo_->currentIndex();
        const int grid_index = grid_combo_->currentIndex();
        if (hw_index < 0 || grid_index < 0 ||
            hw_index >= (int)hw_keys_.size() || grid_index >= (int)grid_keys_.size()) {
            setError("配置无效");
            return;
        }

        const std::string& hw_key = hw_keys_[(size_t)hw_index];
        const std::string& grid_key = grid_keys_[(size_t)grid_index];
        auto hw_it = HARDWARE_PROFILES.find(hw_key);
        auto grid_it = GRID_REGIONS.find(grid_key);
        if (hw_it == HARDWARE_PROFILES.end() || grid_it == GRID_REGIONS.end()) {
            setError("配置无效");
            return;
        }

        try {
            StaticAnalyzer analyzer;
            auto functions = analyzer.analyze(path);
            if (functions.empty()) {
                setError("没有识别到函数定义");
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

        status_->setText("分析完成");
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
        out << "碳排放: " << fmt_co2(fp.estimated_co2_mg) << "\n";
        out << "能耗: " << fmt_energy(fp.estimated_joules) << "\n";
        out << "能耗评分: " << std::fixed << std::setprecision(0) << fp.energy_score << "\n";
        out << "循环: 深度 " << fp.loops.depth << ", 数量 " << fp.loops.count << "\n\n";
        out << "指令计数\n";
        out << "ALU: " << fp.raw.alu << "\n";
        out << "浮点: " << fp.raw.fpu << "\n";
        out << "内存: " << fp.raw.memory << "\n";
        out << "分支: " << fp.raw.branch << "\n";
        out << "IO: " << fp.raw.io << "\n";
        out << "SIMD: " << fp.raw.simd << "\n";
        out << "同步: " << fp.raw.atomic << "\n";

        if (!fp.callees.empty()) {
            out << "\n调用\n";
            for (const auto& callee : fp.callees)
                out << "- " << callee << "\n";
        }
        if (!fp.warnings.empty()) {
            out << "\n警告\n";
            for (const auto& warning : fp.warnings)
                out << "- " << warning << "\n";
        }
        if (!fp.suggestions.empty()) {
            out << "\n建议\n";
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

    QLineEdit* file_edit_ = nullptr;
    QComboBox* hw_combo_ = nullptr;
    QComboBox* grid_combo_ = nullptr;
    QLabel* status_ = nullptr;
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
