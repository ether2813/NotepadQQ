#include "include/EditorNS/editor.h"
#include "include/EditorNS/languageservice.h"
#include "include/EditorNS/nppdefs.h"
#include "include/EditorNS/scintillaview.h"
#include "include/EditorNS/stylemanager.h"
#include "include/editortabwidget.h"
#include "include/topeditorcontainer.h"

#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

#include <ILexer.h>
#include <Lexilla.h>

using namespace EditorNS;

// Returns the value of an (already settled) promise.
template <typename T> static T await(QtPromise::QPromise<T> promise)
{
    T out{};
    promise.then([&out](const T& value) { out = value; }).wait();
    return out;
}

// Tests for the native (Scintilla based) editor that replaced the CodeMirror/WebEngine one.
class EditorTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    void valueRoundTripsAndNormalisesLineEndings();
    void changingLineEndingsConvertsDocument();
    void cursorColumnsAreUtf16();
    void selectionsAndSelectedTexts();
    void setSelectionsTextModes();
    void searchForwardBackwardAndWrap();
    void searchIsCaseInsensitiveWithModifier();
    void replaceUsesGroupTokens();
    void replaceAllCountsAndIsSingleUndoStep();
    void searchSelectAllCreatesMultipleSelections();
    void lineTransformations();
    void cleanStateFollowsSavePointAndUndo();
    void indentationModeAndDetection();
    void lexerIsAppliedForLanguage();
    void everyMappedLanguageHasALexer();
    void nppLangTypesRoundTrip();
    void themesAreLoaded();
    void tabWidgetSwapsDocumentsInOneView();
    void topEditorContainerKeepsStableViews();
    void backgroundEditorKeepsCursorState();

private:
    QTemporaryDir m_settingsDirectory;
};

void EditorTest::initTestCase()
{
    QVERIFY(m_settingsDirectory.isValid());
    QCoreApplication::setOrganizationName("Notepadqq-tests");
    QCoreApplication::setApplicationName("Notepadqq-tests");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDirectory.path());
}

void EditorTest::valueRoundTripsAndNormalisesLineEndings()
{
    Editor editor;
    editor.setEndOfLineSequence("\r\n");
    editor.setValue("first\nsecond\r\nthird\rfourth");
    QCOMPARE(editor.value(), QString("first\nsecond\nthird\nfourth"));
    QCOMPARE(editor.rawText(), QString("first\r\nsecond\r\nthird\r\nfourth"));
    QCOMPARE(await(editor.lineCount()), 4);
    QCOMPARE(editor.eolFormat(), 0);
}

void EditorTest::changingLineEndingsConvertsDocument()
{
    Editor editor;
    editor.setValue("a\nb\nc");
    editor.setEndOfLineSequence("\r");
    QCOMPARE(editor.rawText(), QString("a\rb\rc"));
    QCOMPARE(editor.value(), QString("a\nb\nc"));
    QCOMPARE(editor.eolFormat(), 1);
}

void EditorTest::cursorColumnsAreUtf16()
{
    Editor editor;
    // U+1F600 takes two UTF-16 code units (and four UTF-8 bytes).
    editor.setValue(QString("x") + QString::fromUcs4(U"\U0001F600") + "yz\n\u00E9t\u00E9");
    editor.setCursorPosition(0, 3);
    QCOMPARE(editor.cursorPosition(), qMakePair(0, 3));
    editor.setSelection(0, 3, 0, 4);
    QCOMPARE(await(editor.selectedTexts()), QStringList{"y"});
    editor.setSelection(1, 1, 1, 3);
    QCOMPARE(await(editor.selectedTexts()), QStringList{"t\u00E9"});
    // Columns past the end of a line clamp to the line end.
    editor.setCursorPosition(1, 99);
    QCOMPARE(editor.cursorPosition(), qMakePair(1, 3));
}

void EditorTest::selectionsAndSelectedTexts()
{
    Editor editor;
    editor.setValue("hello world\nsecond line");
    editor.setSelection(0, 6, 1, 6);
    const QList<Selection> sels = editor.selections();
    QCOMPARE(sels.size(), 1);
    QCOMPARE(sels[0].from.line, 0);
    QCOMPARE(sels[0].from.column, 6);
    QCOMPARE(sels[0].to.line, 1);
    QCOMPARE(sels[0].to.column, 6);
    QCOMPARE(await(editor.selectedTexts()), QStringList{"world\nsecond"});

    const QVariantList wire = editor.dispatch("C_FUN_GET_SELECTIONS", 0).toList();
    QCOMPARE(wire.size(), 1);
    QCOMPARE(wire[0].toMap()["head"].toMap()["line"].toInt(), 1);
}

void EditorTest::setSelectionsTextModes()
{
    Editor editor;
    editor.setValue("abc def");
    editor.setSelection(0, 4, 0, 7);
    editor.setSelectionsText({"XYZW"}, Editor::SelectMode::Selected);
    QCOMPARE(editor.value(), QString("abc XYZW"));
    QCOMPARE(await(editor.selectedTexts()), QStringList{"XYZW"});

    editor.setSelectionsText({"q"}, Editor::SelectMode::Before);
    QCOMPARE(editor.value(), QString("abc q"));
    QCOMPARE(editor.cursorPosition(), qMakePair(0, 4));

    editor.setSelection(0, 0, 0, 3);
    editor.setSelectionsText({"one", "two"}, Editor::SelectMode::After);
    QCOMPARE(editor.value(), QString("one\ntwo q"));
}

void EditorTest::searchForwardBackwardAndWrap()
{
    Editor editor;
    editor.setValue("foo bar foo baz foo");
    editor.setCursorPosition(0, 0);

    QVariant r = editor.dispatch("C_FUN_SEARCH", QVariantList{"foo", "m", true});
    QVERIFY(r.toStringList() == QStringList{"foo"});
    QCOMPARE(editor.selections()[0].to.column, 3);

    editor.dispatch("C_FUN_SEARCH", QVariantList{"foo", "m", true});
    QCOMPARE(editor.selections()[0].from.column, 8);
    editor.dispatch("C_FUN_SEARCH", QVariantList{"foo", "m", true});
    QCOMPARE(editor.selections()[0].from.column, 16);
    // Wraps around to the first match.
    editor.dispatch("C_FUN_SEARCH", QVariantList{"foo", "m", true});
    QCOMPARE(editor.selections()[0].from.column, 0);

    // Backwards from the first match wraps to the last one; backward selections keep the
    // caret at the start of the match.
    editor.dispatch("C_FUN_SEARCH", QVariantList{"foo", "m", false});
    QCOMPARE(editor.selections()[0].to.column, 16);
    editor.dispatch("C_FUN_SEARCH", QVariantList{"foo", "m", false});
    QCOMPARE(editor.selections()[0].to.column, 8);

    QCOMPARE(editor.dispatch("C_FUN_SEARCH", QVariantList{"nothing", "m", true}).toBool(), false);
}

void EditorTest::searchIsCaseInsensitiveWithModifier()
{
    Editor editor;
    editor.setEndOfLineSequence("\r\n");
    editor.setValue("Alpha\nBETA\ngamma");
    editor.setCursorPosition(0, 0);
    QCOMPARE(editor.dispatch("C_FUN_SEARCH", QVariantList{"beta", "m", true}).toBool(), false);
    QVERIFY(!editor.dispatch("C_FUN_SEARCH", QVariantList{"beta", "mi", true}).toStringList().isEmpty());
    QCOMPARE(editor.selections()[0].from.line, 1);
    // '$' matches before CRLF line terminators.
    editor.setCursorPosition(0, 0);
    QVERIFY(!editor.dispatch("C_FUN_SEARCH", QVariantList{"a$", "m", true}).toStringList().isEmpty());
    QCOMPARE(editor.selections()[0].from.line, 0);
    QCOMPARE(editor.selections()[0].from.column, 4);
}

void EditorTest::replaceUsesGroupTokens()
{
    Editor editor;
    editor.setValue("key=value; other=thing");
    editor.setCursorPosition(0, 0);
    editor.dispatch("C_FUN_SEARCH", QVariantList{"(\\w+)=(\\w+)", "m", true});
    editor.dispatch("C_FUN_REPLACE", QVariantList{"(\\w+)=(\\w+)", "m", true, "\\2:\\1\\3", "3"});
    QCOMPARE(editor.value(), QString("value:key; other=thing"));
    // The next match is selected after replacing.
    QCOMPARE(await(editor.selectedTexts()), QStringList{"other=thing"});
}

void EditorTest::replaceAllCountsAndIsSingleUndoStep()
{
    Editor editor;
    editor.setValue("a1 b22 c333");
    editor.dispatch("C_CMD_CLEAR_HISTORY", 0);
    const int count = editor.dispatch("C_FUN_REPLACE_ALL", QVariantList{"(\\d+)", "m", "<\\1>", "3"}).toInt();
    QCOMPARE(count, 3);
    QCOMPARE(editor.value(), QString("a<1> b<22> c<333>"));
    editor.dispatch("C_CMD_UNDO", 0);
    QCOMPARE(editor.value(), QString("a1 b22 c333"));

    // Empty matches (e.g. "^") prefix every line.
    editor.setValue("x\ny");
    QCOMPARE(editor.dispatch("C_FUN_REPLACE_ALL", QVariantList{"^", "m", "// ", "3"}).toInt(), 2);
    QCOMPARE(editor.value(), QString("// x\n// y"));
}

void EditorTest::searchSelectAllCreatesMultipleSelections()
{
    Editor editor;
    editor.setValue("cat dog cat bird cat");
    QCOMPARE(editor.dispatch("C_FUN_SEARCH_SELECT_ALL", QVariantList{"cat", "m"}).toInt(), 3);
    QCOMPARE(editor.selections().size(), 3);
    QCOMPARE(await(editor.selectedTexts()), (QStringList{"cat", "cat", "cat"}));
    editor.setSelectionsText({"1", "2", "3"}, Editor::SelectMode::After);
    QCOMPARE(editor.value(), QString("1 dog 2 bird 3"));
}

void EditorTest::lineTransformations()
{
    Editor editor;
    editor.setCustomIndentationMode(true, 4);
    editor.setValue("  lead\ntrail  \n\tx\ty");
    editor.dispatch("C_CMD_TRIM_TRAILING_SPACE", 0);
    QCOMPARE(editor.value(), QString("  lead\ntrail\n\tx\ty"));
    editor.dispatch("C_CMD_TRIM_LEADING_SPACE", 0);
    QCOMPARE(editor.value(), QString("lead\ntrail\nx\ty"));
    editor.dispatch("C_CMD_TAB_TO_SPACE", 0);
    QCOMPARE(editor.value(), QString("lead\ntrail\nx   y"));
    editor.setValue("        eight");
    editor.dispatch("C_CMD_SPACE_TO_TAB_LEADING", 0);
    QCOMPARE(editor.value(), QString("\t\teight"));
    editor.setValue("a\nb");
    editor.dispatch("C_CMD_EOL_TO_SPACE", 0);
    QCOMPARE(editor.value(), QString("a b"));
}

void EditorTest::cleanStateFollowsSavePointAndUndo()
{
    Editor editor;
    QSignalSpy cleanSpy(&editor, &Editor::cleanChanged);
    editor.setValue("text");
    editor.dispatch("C_CMD_CLEAR_HISTORY", 0);
    editor.markClean();
    QVERIFY(editor.isClean());

    editor.setSelectionsText({"more "}, Editor::SelectMode::After);
    QVERIFY(!editor.isClean());
    const int generation = await(editor.getHistoryGeneration());
    editor.dispatch("C_CMD_UNDO", 0);
    QVERIFY(editor.isClean());
    QVERIFY(await(editor.getHistoryGeneration()) < generation);

    editor.markDirty();
    QVERIFY(!editor.isClean());
    editor.markClean();
    QVERIFY(editor.isClean());
    QVERIFY(cleanSpy.count() > 0);
}

void EditorTest::indentationModeAndDetection()
{
    Editor editor;
    editor.setCustomIndentationMode(false, 2);
    QCOMPARE(editor.indentationMode().useTabs, false);
    QCOMPARE(editor.indentationMode().size, 2);

    editor.setValue("int main() {\n    return 0;\n}");
    auto detected = await(editor.detectDocumentIndentation());
    QVERIFY(detected.second);
    QCOMPARE(detected.first.useTabs, false);
    QCOMPARE(detected.first.size, 4);

    editor.setValue("a:\n\tb");
    detected = await(editor.detectDocumentIndentation());
    QVERIFY(detected.second);
    QVERIFY(detected.first.useTabs);
}

void EditorTest::lexerIsAppliedForLanguage()
{
    Editor editor;
    QSignalSpy langSpy(&editor, &Editor::currentLanguageChanged);
    editor.setLanguageFromFilePath("/tmp/example.cpp");
    QCOMPARE(editor.getLanguage()->id, QString("cpp"));
    QCOMPARE(langSpy.count(), 1);
    char lexerName[64] = {0};
    editor.sci(SCI_GETLEXERLANGUAGE, 0, reinterpret_cast<sptr_t>(lexerName));
    QCOMPARE(QByteArray(lexerName), QByteArray("cpp"));

    // Notepad++-only languages are available too.
    editor.setLanguageFromFilePath("/tmp/script.bat");
    QCOMPARE(editor.getLanguage()->nppName, QString("batch"));
    editor.setLanguageFromFilePath("/tmp/setup.iss");
    QCOMPARE(editor.getLanguage()->nppName, QString("inno"));

    // Shebang detection still works.
    editor.setValue("#!/usr/bin/env python3\nprint(1)\n");
    QCOMPARE(editor.getLanguage()->nppName, QString("python"));
}

void EditorTest::everyMappedLanguageHasALexer()
{
    for (const Language& lang : LanguageService::getInstance().languages()) {
        if (lang.lexer.isEmpty())
            continue;
        Scintilla::ILexer5* lexer = CreateLexer(lang.lexer.toLatin1().constData());
        QVERIFY2(lexer != nullptr, qPrintable(lang.id + " -> " + lang.lexer));
        lexer->Release();
    }
    QVERIFY(LanguageService::getInstance().lookupById("plaintext") != nullptr);
}

void EditorTest::nppLangTypesRoundTrip()
{
    for (const NppLangDef& def : nppLanguages()) {
        QCOMPARE(nppLangDefByType(def.langType), &def);
        QCOMPARE(nppLangDefByName(QString::fromLatin1(def.name)), &def);
    }
    const Language* cpp = LanguageService::getInstance().lookupByNppLangType(3);
    QVERIFY(cpp);
    QCOMPARE(cpp->id, QString("cpp"));
    const Language* text = LanguageService::getInstance().lookupByNppLangType(0);
    QVERIFY(text);
    QCOMPARE(text->id, QString("plaintext"));
}

void EditorTest::themesAreLoaded()
{
    const QStringList names = StyleManager::instance().themeNames();
    QCOMPARE(names.first(), QString("default"));
    QVERIFY(names.contains("Monokai"));
    QVERIFY(names.contains("DarkModeDefault"));
    QVERIFY(!StyleManager::instance().theme("default").isDark());
    QVERIFY(StyleManager::instance().theme("Monokai").isDark());
    QVERIFY(!StyleManager::instance().theme("default").lexerStyles.value("cpp").isEmpty());
    QVERIFY(StyleManager::instance().nppLanguage("cpp") != nullptr);
}

void EditorTest::tabWidgetSwapsDocumentsInOneView()
{
    EditorTabWidget tabs;
    tabs.resize(640, 480);
    tabs.show();
    const int first = tabs.addEditorTab(true, "one");
    const int second = tabs.addEditorTab(true, "two");
    Editor* a = tabs.editor(first);
    Editor* b = tabs.editor(second);
    a->setValue("document A\nline 2\nline 3");
    b->setValue("document B");

    ScintillaView* view = tabs.scintillaView();
    QVERIFY(view);
    QCOMPARE(b->view(), view);
    QCOMPARE(view->document(), b->document());
    QVERIFY(a->view() == nullptr);

    tabs.setCurrentIndex(first);
    QCOMPARE(tabs.scintillaView(), view);
    QCOMPARE(view->document(), a->document());
    a->setSelection(1, 0, 1, 4);

    tabs.setCurrentIndex(second);
    QCOMPARE(view->document(), b->document());
    // A's selection is remembered while it is not displayed...
    QCOMPARE(await(a->selectedTexts()), QStringList{"line"});
    tabs.setCurrentIndex(first);
    // ...and restored in the view.
    QCOMPARE(view->send(SCI_GETSELECTIONSTART), a->sci(SCI_GETSELECTIONSTART));
    QCOMPARE(await(a->selectedTexts()), QStringList{"line"});

    // Closing the tab that displays the view must not destroy the view.
    QPointer<ScintillaView> guard(view);
    tabs.closeEditorTab(first);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(guard);
    QCOMPARE(view->document(), b->document());
}

void EditorTest::topEditorContainerKeepsStableViews()
{
    auto container = std::make_unique<TopEditorContainer>();
    ScintillaView* main = container->scintillaView(0);
    ScintillaView* second = container->scintillaView(1);
    QVERIFY(main && second && main != second);
    QCOMPARE(container->scintillaViewIndex(container->tabWidget(0)), 0);

    EditorTabWidget* other = container->addTabWidget();
    QCOMPARE(container->scintillaViewIndex(other), 1);
    other->addEditorTab(true, "x");
    QCOMPARE(other->currentEditor()->view(), second);

    // Removing the second pane releases its view, which survives for the next pane.
    other->closeEditorTab(0);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    EditorTabWidget::deleteIfEmpty(other);
    QPointer<ScintillaView> guard(second);
    QVERIFY(guard);
    EditorTabWidget* again = container->addTabWidget();
    QCOMPARE(again->scintillaView(), second);
}

void EditorTest::backgroundEditorKeepsCursorState()
{
    Editor editor; // never shown: all operations go through the hidden Scintilla instance
    editor.setValue("one\ntwo\nthree");
    editor.setCursorPosition(2, 2);
    QCOMPARE(editor.cursorPosition(), qMakePair(2, 2));
    editor.setScrollPosition(0, 0);
    QCOMPARE(editor.scrollPosition(), qMakePair(0, 0));
    QCOMPARE(editor.dispatch("C_FUN_GET_CURSOR", 0).toList(), (QVariantList{2, 2}));
    QCOMPARE(editor.dispatch("C_FUN_GET_CURRENT_WORD", 0).toString(), QString("three"));
}

QTEST_MAIN(EditorTest)

#include "tst_editor.moc"
