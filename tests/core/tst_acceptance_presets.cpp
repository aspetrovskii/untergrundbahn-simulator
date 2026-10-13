#include "api/Functions.h"

#include <QtTest>
#include <string>
#include <vector>

class AcceptancePresetsTest : public QObject {
    Q_OBJECT

private slots:
    void builtinPresetsValidate();
    void defaultParamsValidate();
};

void AcceptancePresetsTest::builtinPresetsValidate() {
    const std::vector<ubahn::api::Preset> presets = ubahn::api::builtinPresets();
    QVERIFY(!presets.empty());
    for (const ubahn::api::Preset& preset : presets) {
        const std::vector<ubahn::api::ValidationIssue> issues = ubahn::api::validate(preset.params);
        std::string text = preset.id;
        for (const ubahn::api::ValidationIssue& issue : issues) {
            text += " ";
            text += issue.field;
            text += ": ";
            text += issue.message;
        }
        QVERIFY2(issues.empty(), text.c_str());
    }
}

void AcceptancePresetsTest::defaultParamsValidate() {
    const std::vector<ubahn::api::ValidationIssue> issues =
        ubahn::api::validate(ubahn::api::defaultParams());
    QVERIFY(issues.empty());
}

QTEST_APPLESS_MAIN(AcceptancePresetsTest)
#include "tst_acceptance_presets.moc"
