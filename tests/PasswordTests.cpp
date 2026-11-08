#include "ui/ArchiveController.h"
#include "ui/ArchivePasswords.h"
#include "ui/PasswordLibrary.h"
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

namespace {
bool Write(const QString& path, const QByteArray& bytes) {
    QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray Read(const QString& path) {
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {}; return file.readAll();
}
QString MakeArchive(const QString& root, const QString& name, const QString& format, const QString& password, bool header = false, uint64_t volume = 0) {
    const auto source = root + "/payload.txt";
    if (!Write(source, QByteArray(50000, 'p') + "complete payload")) return {};
    SevenZipCore core;
    CompressionOptions options; options.format = format.toStdWString(); options.password = password.toStdWString();
    options.encryptHeaders = header; options.volumeSize = volume; options.level = volume ? 0 : 5;
    const auto target = root + '/' + name + '.' + format;
    if (!core.Compress({source.toStdWString()}, target.toStdWString(), options)) return {};
    return volume ? target + ".001" : target;
}
}

class PasswordTests final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName("7ZipContextTests");
        QCoreApplication::setApplicationName("7ZipContextTests");
    }
    void parsePreservesPasswordsAndDecodesFiles() {
        QStringList entries; QString error;
        QVERIFY(PasswordLibrary::parse(QString(QChar::ByteOrderMark) + QStringLiteral("one\r\n\r\n  密码  \r\none\nONE\n "), entries, error));
        QCOMPARE(entries, QStringList({"one", QStringLiteral("  密码  "), "ONE", " "}));
        QVERIFY(!PasswordLibrary::parse({}, entries, error));
        QVERIFY(!PasswordLibrary::parse(QString(1025, 'a'), entries, error));
        QVERIFY(!PasswordLibrary::parse(QStringLiteral("a") + QChar::Null + 'b', entries, error));
        QTemporaryDir temporary;
        const QString text = QStringLiteral("密码甲\r\n 密码乙 \r\n");
        QList<QByteArray> encodings{QByteArray::fromHex("efbbbf") + text.toUtf8(), QByteArray::fromHex("fffe"), QByteArray::fromHex("feff")};
        for (const auto character : text) {
            const auto value = character.unicode();
            encodings[1].append(static_cast<char>(value & 255)); encodings[1].append(static_cast<char>(value >> 8));
            encodings[2].append(static_cast<char>(value >> 8)); encodings[2].append(static_cast<char>(value & 255));
        }
        const auto wide = text.toStdWString();
        const auto count = WideCharToMultiByte(54936, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
        QVERIFY(count > 0); QByteArray gb(count, '\0');
        QVERIFY(WideCharToMultiByte(54936, 0, wide.data(), static_cast<int>(wide.size()), gb.data(), count, nullptr, nullptr) > 0);
        encodings.append(gb);
        for (int i = 0; i < encodings.size(); ++i) {
            const auto path = temporary.filePath(QString::number(i) + ".txt"); QVERIFY(Write(path, encodings[i]));
            QString decoded; QVERIFY2(PasswordLibrary::readTextFile(path, decoded, error), qPrintable(error));
            QVERIFY(PasswordLibrary::parse(decoded, entries, error));
            QCOMPARE(entries, QStringList({QStringLiteral("密码甲"), QStringLiteral(" 密码乙 ")}));
        }
    }
    void protectedLibraryRoundtripAndCorruptionProtection() {
        QTemporaryDir temporary; const auto path = temporary.filePath("password-lists.dat");
        QString error;
        PasswordLibrary library(path); QVERIFY(library.load(error));
        QList<PasswordList> lists{{"a", QStringLiteral("常用密码"), true, {"example-private-value", " second value "}},
                                  {"b", QStringLiteral("备用"), false, {"disabled-value"}}};
        lists[0].comments.insert("example-private-value", QStringLiteral("用于分卷归档"));
        QVERIFY2(library.replace(lists, error), qPrintable(error));
        auto bytes = Read(path); QVERIFY(bytes.startsWith("7ZCPWD01"));
        QVERIFY(!bytes.contains("example-private-value")); QVERIFY(!bytes.contains(QStringLiteral("常用密码").toUtf8()));
        QVERIFY(!bytes.contains(QStringLiteral("用于分卷归档").toUtf8()));
        PasswordLibrary restored(path); QVERIFY2(restored.load(error), qPrintable(error));
        QCOMPARE(restored.lists().size(), 2); QCOMPARE(restored.lists()[0].entries, lists[0].entries);
        QCOMPARE(restored.lists()[0].comments, lists[0].comments);
        QCOMPARE(restored.enabledCount(), 1); QCOMPARE(restored.enabledPasswords().size(), 2);
        QVERIFY(!restored.lists()[1].enabled);
        bytes[bytes.size() - 1] = static_cast<char>(bytes.back() ^ 0x55); QVERIFY(Write(path, bytes));
        PasswordLibrary corrupted(path); QVERIFY(!corrupted.load(error)); QVERIFY(!corrupted.replace({}, error));
        QCOMPARE(Read(path), bytes);
    }
    void passwordCommentsSurviveTextEditsAndDoNotBecomeCandidates() {
        QTemporaryDir temp; ArchiveController controller(nullptr, temp.filePath("library.dat"));
        QHash<QString, QString> notes{{"secret", QStringLiteral("来自下载目录")}, {" keep spaces ", QStringLiteral("备用")}};
        QVERIFY(controller.addPasswordList("list", "secret\n keep spaces ", &notes));
        const auto id = controller.passwordLibrary().lists().first().id;
        QCOMPARE(controller.passwordLibrary().enabledPasswords(), QStringList({"secret", " keep spaces "}));
        QVERIFY(controller.editPasswordList(id, "renamed", "secret\nnew"));
        QCOMPARE(controller.passwordLibrary().lists().first().comments.value("secret"), QStringLiteral("来自下载目录"));
        QVERIFY(!controller.passwordLibrary().lists().first().comments.contains(" keep spaces "));
        QHash<QString, QString> invalid{{"secret", QString(2049, 'x')}};
        QVERIFY(!controller.editPasswordList(id, "renamed", "secret\nnew", &invalid));
        QCOMPARE(controller.passwordLibrary().lists().first().comments.value("secret"), QStringLiteral("来自下载目录"));
    }
    void uniquePasswordsKeepAllSourcesAndExactValues() {
        QTemporaryDir temp; QString error; PasswordLibrary library(temp.filePath("library.dat")); QVERIFY(library.load(error));
        QList<PasswordList> lists{{"a", "first", false, {"shared", " shared "}, {{"shared", "note-one"}}},
            {"b", "second", true, {"shared", "SHARED"}, {{"shared", "note-two"}}},
            {"c", "third", false, {"shared", "shared"}, {}}};
        QVERIFY2(library.replace(lists, error), qPrintable(error));
        PasswordLibrary restored(library.path()); QVERIFY(restored.load(error));
        const auto& unique = restored.uniquePasswords(); QCOMPARE(unique.size(), 3);
        QCOMPARE(unique[0].value, QStringLiteral("shared")); QCOMPARE(unique[0].sources.size(), 3); QVERIFY(unique[0].enabled);
        QCOMPARE(unique[0].sources[0].comment, QStringLiteral("note-one")); QVERIFY(!unique[0].sources[0].enabled);
        QCOMPARE(unique[0].sources[1].comment, QStringLiteral("note-two")); QVERIFY(unique[0].sources[1].enabled);
        QCOMPARE(unique[1].value, QStringLiteral(" shared ")); QVERIFY(!unique[1].enabled);
        QCOMPARE(unique[2].value, QStringLiteral("SHARED")); QVERIFY(unique[2].enabled);
        QCOMPARE(restored.enabledPasswords(), QStringList({"shared", "SHARED"}));
        QCOMPARE(restored.lists()[2].entries, QStringList({"shared", "shared"}));
    }
    void reimportKeepsNotesAndDoesNotCreateAnotherList() {
        QTemporaryDir temp; ArchiveController controller(nullptr, temp.filePath("library.dat"));
        const auto input = temp.filePath("source.txt"); QVERIFY(Write(input, "one\ntwo\none\n"));
        QVERIFY(controller.importPasswordList(input)); const auto id = controller.passwordLibrary().lists()[0].id;
        const QHash<QString, QString> notes{{"one", "remember this source"}};
        QVERIFY(controller.editPasswordList(id, "source", "one\ntwo", &notes));
        QVERIFY(controller.setPasswordListEnabled(id, false));
        QVERIFY(Write(input, "two\none\ntwo\n")); QVERIFY(controller.importPasswordList(input));
        QCOMPARE(controller.passwordLibrary().lists().size(), 1); QCOMPARE(controller.passwordLibrary().lists()[0].id, id);
        QVERIFY(controller.passwordLibrary().lists()[0].enabled);
        QCOMPARE(controller.passwordLibrary().lists()[0].comments, notes);
        QVERIFY(controller.importPasswordList(input)); QCOMPARE(controller.passwordLibrary().lists().size(), 1);
        QVERIFY(Write(input, "one\nthree\n")); QVERIFY(controller.importPasswordList(input));
        QCOMPARE(controller.passwordLibrary().uniquePasswords().size(), 3);
    }
    void enabledOrderingAndDeduplication() {
        QList<PasswordList> lists{{"a", "first", true, {"duplicate", "shared", "right"}},
                                 {"b", "second", true, {"shared", "other"}}, {"c", "disabled", false, {"disabled-password"}}};
        const auto candidates = BuildPasswordCandidates("manual", lists, true, "right", "shared");
        QStringList values; for (const auto& candidate : candidates) values.append(candidate.value);
        QCOMPARE(values, QStringList({"manual", "right", "shared", "duplicate", "other"}));
        QCOMPARE(BuildPasswordCandidates("manual", lists, false, "right").size(), 1);
        QVERIFY(BuildPasswordCandidates({}, lists, false, "right").isEmpty());
        lists[0].enabled = false;
        const auto disabled = BuildPasswordCandidates({}, lists, true, "right");
        for (const auto& candidate : disabled) QVERIFY(candidate.value != "right" && candidate.value != "disabled-password");
    }
    void flatPasswordEditsCoverLegacyCopiesAndPreserveExactValues() {
        QTemporaryDir temp; const auto path = temp.filePath("library.dat");
        ArchiveController controller(nullptr, path);
        const QHash<QString, QString> notesA{{"shared", "first note"}}, notesB{{"shared", "second note"}};
        QVERIFY(controller.addPasswordList("legacy A", "shared\n  exact  \nSHARED", &notesA));
        QVERIFY(controller.addPasswordList("legacy B", "shared\ndisabled-a\ndisabled-b", &notesB));
        QVERIFY(controller.setPasswordListEnabled(controller.passwordLibrary().lists().last().id, false));
        QVERIFY(controller.editPassword("shared", " changed "));
        for (const auto& list : controller.passwordLibrary().lists()) {
            QVERIFY(!list.entries.contains("shared")); QVERIFY(!list.comments.contains("shared"));
            QVERIFY(list.entries.contains(" changed "));
        }
        QCOMPARE(controller.passwordLibrary().uniquePasswords().first().value, QString(" changed "));
        QCOMPARE(controller.passwordLibrary().uniquePasswords().first().sources.size(), 2);
        QVERIFY(controller.setPasswordEnabled(" changed ", false));
        QVERIFY(!controller.passwordLibrary().enabledPasswords().contains(" changed "));
        QVERIFY(controller.passwordLibrary().enabledPasswords().contains("  exact  "));
        QVERIFY(controller.passwordLibrary().enabledPasswords().contains("SHARED"));
        QVERIFY(controller.addPasswords("disabled-a\nnew\nnew\n  exact  "));
        QVERIFY(controller.passwordLibrary().enabledPasswords().contains("disabled-a"));
        QVERIFY(!controller.passwordLibrary().enabledPasswords().contains("disabled-b"));
        const auto saved = Read(path);
        QVERIFY(controller.addPasswords("new\nnew\n  exact  ")); QCOMPARE(Read(path), saved);
        QVERIFY(!controller.editPassword("new", "")); QVERIFY(!controller.editPassword("new", "bad\npassword"));
        QCOMPARE(Read(path), saved);
        QVERIFY(controller.removePasswords({" changed ", "new", "new"}));
        for (const auto& list : controller.passwordLibrary().lists()) {
            QVERIFY(!list.entries.contains(" changed ")); QVERIFY(!list.entries.contains("new"));
        }
        QString error; PasswordLibrary restored(path); QVERIFY(restored.load(error));
        QCOMPARE(restored.enabledPasswords(), controller.passwordLibrary().enabledPasswords());
        QVERIFY(restored.enabledPasswords().contains("  exact  ")); QVERIFY(!restored.enabledPasswords().contains("exact"));
        QStringList all; for (const auto& entry : restored.uniquePasswords()) all << entry.value;
        QVERIFY(controller.removePasswords(all)); QVERIFY(controller.passwordLibrary().uniquePasswords().isEmpty());
        QVERIFY(restored.load(error)); QVERIFY(restored.uniquePasswords().isEmpty());
    }
    void flatPasswordImportMergesFilesAndPreservesUnreadableStorage() {
        QTemporaryDir temp; const auto first = temp.filePath("first.txt"), second = temp.filePath("second.txt");
        QVERIFY(Write(first, "one\ntwo\none\n")); QVERIFY(Write(second, "two\nthree\n"));
        ArchiveController controller(nullptr, temp.filePath("library.dat"));
        QVERIFY(controller.importPasswords(first)); QVERIFY(controller.importPasswords(second));
        QCOMPARE(controller.passwordLibrary().enabledPasswords(), QStringList({"one", "two", "three"}));
        QVERIFY(controller.editPassword("one", "two"));
        QCOMPARE(controller.passwordLibrary().enabledPasswords(), QStringList({"two", "three"}));
        QCOMPARE(Read(first), QByteArray("one\ntwo\none\n"));
        const auto broken = temp.filePath("broken.dat"); QVERIFY(Write(broken, "invalid encrypted library"));
        ArchiveController unreadable(nullptr, broken); QVERIFY(!unreadable.addPasswords("new"));
        QCOMPARE(Read(broken), QByteArray("invalid encrypted library"));
    }
    void flatActionsPreserveLongLegacyNotesWhenValuesMerge() {
        QTemporaryDir temp; ArchiveController controller(nullptr, temp.filePath("library.dat"));
        const QString first(1800, 'a'), second(1800, 'b'), third(1800, 'c');
        const QHash<QString, QString> a{{"old", first}, {"new", second}}, b{{"old", third}};
        QVERIFY(controller.addPasswordList("A", "old\nnew\nkeep", &a));
        QVERIFY(controller.addPasswordList("B", "old\nother", &b));
        QVERIFY(controller.setPasswordEnabled("old", false)); QVERIFY(controller.addPasswords("old"));
        QVERIFY(controller.editPassword("old", "new"));
        QStringList notes;
        for (const auto& entry : controller.passwordLibrary().uniquePasswords()) if (entry.value == "new")
            for (const auto& source : entry.sources) if (!source.comment.isEmpty()) notes << source.comment;
        QCOMPARE(notes.size(), 3); QVERIFY(notes.contains(first)); QVERIFY(notes.contains(second)); QVERIFY(notes.contains(third));
        QVERIFY(controller.setPasswordEnabled("new", false));
        QVERIFY(!controller.passwordLibrary().enabledPasswords().contains("new"));
        QVERIFY(controller.passwordLibrary().enabledPasswords().contains("keep"));
        QVERIFY(controller.passwordLibrary().enabledPasswords().contains("other"));
    }
    void encryptedDataAndHeaders_data() {
        QTest::addColumn<QString>("format"); QTest::addColumn<bool>("header"); QTest::addColumn<bool>("volumes");
        QTest::newRow("zip-aes") << QString("zip") << false << false;
        QTest::newRow("7z-data") << QString("7z") << false << false;
        QTest::newRow("7z-encrypted-headers") << QString("7z") << true << false;
        QTest::newRow("7z-encrypted-volumes") << QString("7z") << true << true;
    }
    void encryptedDataAndHeaders() {
        QFETCH(QString, format); QFETCH(bool, header); QFETCH(bool, volumes);
        QTemporaryDir temporary;
        const QString password = format == "zip" ? QStringLiteral(" zip-test-pass ") : QStringLiteral(" 密码-for-tests ");
        const auto archive = MakeArchive(temporary.path(), "encrypted", format, password, header, volumes ? 8192 : 0); QVERIFY(!archive.isEmpty());
        SevenZipCore core;
        if (header) { QVERIFY(!core.OpenArchive(archive.toStdWString())); QVERIFY(core.NeedsPassword()); }
        QList<PasswordList> lists{{"a", "test-list", true, {"incorrect-1", "incorrect-2", password}}};
        const auto candidates = BuildPasswordCandidates("manual-incorrect", lists, true);
        const auto result = ResolveArchivePassword(core, archive.toStdWString(), candidates, [](uint64_t, uint64_t) { return true; });
        QCOMPARE(result.state, ArchivePasswordResult::State::Ready); QVERIFY(result.verified);
        QCOMPARE(result.attempts, 4); QCOMPARE(result.password, password); QCOMPARE(result.source, QStringLiteral("密码列表"));
        const auto output = temporary.filePath("extracted"); QVERIFY(!QFileInfo::exists(output));
        QVERIFY2(core.Extract(output.toStdWString(), result.password.toStdWString()), qPrintable(QString::fromStdWString(core.LastError())));
        QCOMPARE(Read(output + "/payload.txt"), Read(temporary.filePath("payload.txt"))); core.CloseArchive();
        const auto missed = ResolveArchivePassword(core, archive.toStdWString(), {{"wrong", "test-list"}}, {});
        QCOMPARE(missed.state, ArchivePasswordResult::State::NoMatch); QCOMPARE(missed.attempts, 1); QVERIFY(!core.IsOpen());
    }
    void noPasswordAndCancellation() {
        QTemporaryDir temporary; SevenZipCore core;
        const auto plain = MakeArchive(temporary.path(), "plain", "zip", {}); QVERIFY(!plain.isEmpty());
        const auto noPassword = ResolveArchivePassword(core, plain.toStdWString(), {{"irrelevant", "list"}}, {});
        QCOMPARE(noPassword.state, ArchivePasswordResult::State::Ready); QCOMPARE(noPassword.attempts, 0); QVERIFY(noPassword.password.isEmpty()); core.CloseArchive();
        const auto encrypted = MakeArchive(temporary.path(), "headers", "7z", "not-in-list", true); QVERIFY(!encrypted.isEmpty());
        bool cancelled = false;
        const auto stopped = ResolveArchivePassword(core, encrypted.toStdWString(), {{"wrong-a", "list"}, {"wrong-b", "list"}},
            [&](uint64_t, uint64_t) { return !cancelled; }, [&](int attempt, int) { if (attempt == 2) cancelled = true; });
        QCOMPARE(stopped.state, ArchivePasswordResult::State::Cancelled); QVERIFY(!core.IsOpen());
        const auto invalidPath = temporary.filePath("invalid.zip"); QVERIFY(Write(invalidPath, "plain text"));
        const auto invalid = ResolveArchivePassword(core, invalidPath.toStdWString(), {{"guess", "list"}}, {});
        QCOMPARE(invalid.state, ArchivePasswordResult::State::Invalid); QCOMPARE(invalid.attempts, 0);
    }
    void unsupportedEncryptedCodecDoesNotExhaustPasswordList() {
        QTemporaryDir temp;
        const auto archive = MakeArchive(temp.path(), "unsupported", "zip", "secret"); QVERIFY(!archive.isEmpty());
        auto bytes = Read(archive); const auto aes = QByteArray::fromHex("01990700"); int changed = 0;
        for (qsizetype at = bytes.indexOf(aes); at >= 0; at = bytes.indexOf(aes, at + 11)) {
            QVERIFY(at + 11 <= bytes.size()); bytes[at + 9] = char(0xff); bytes[at + 10] = char(0xff); ++changed;
        }
        QCOMPARE(changed, 2); QVERIFY(Write(archive, bytes));
        SevenZipCore core;
        const auto result = ResolveArchivePassword(core, archive.toStdWString(), {{"wrong", "list"}, {"secret", "list"}}, {});
        QCOMPARE(result.state, ArchivePasswordResult::State::Invalid); QCOMPARE(result.attempts, 0);
        QVERIFY(result.error.contains(QStringLiteral("不支持"))); QVERIFY(!core.IsOpen());
    }
    void controllerAutomaticallyExtractsAndRemembersMatches() {
        QTemporaryDir temporary;
        const QString password = QStringLiteral("controller-test-password");
        const auto first = MakeArchive(temporary.path(), "first", "zip", password);
        const auto second = MakeArchive(temporary.path(), "second", "7z", password, true);
        QVERIFY(!first.isEmpty() && !second.isEmpty());
        ArchiveController controller(nullptr, temporary.filePath("library.dat")); controller.autoPasswords = true;
        QVERIFY(controller.addPasswordList("wrong-list", "wrong"));
        controller.addPaths({first, second}, false); controller.extractDestination = temporary.filePath("result");
        QVERIFY(controller.start(ArchiveController::Operation::Extract));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 20000);
        QVERIFY(!controller.tasks.last().success); QVERIFY(!QFileInfo::exists(controller.extractDestination));
        QVERIFY(controller.addPasswordList("correct-list", password + '\n' + password));
        QSignalSpy statuses(&controller, &ArchiveController::statusChanged);
        QVERIFY(controller.start(ArchiveController::Operation::Extract));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 20000);
        QVERIFY2(controller.tasks.last().success, qPrintable(controller.tasks.last().detail)); QVERIFY(controller.tasks[controller.tasks.size() - 2].success);
        QCOMPARE(controller.passwordAttempt(), 1);
        QCOMPARE(Read(controller.extractDestination + "/first/payload.txt"), Read(temporary.filePath("payload.txt")));
        QCOMPARE(Read(controller.extractDestination + "/second/payload.txt"), Read(temporary.filePath("payload.txt")));
        for (const auto& task : controller.tasks) QVERIFY(!task.detail.contains(password));
        for (const auto& status : statuses) QVERIFY(!status[0].toString().contains(password));
        controller.clear(false); controller.addPaths({first}, false);
        QVERIFY(controller.start(ArchiveController::Operation::Test));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 20000); QCOMPARE(controller.passwordAttempt(), 1);
        const auto id = controller.passwordLibrary().lists().last().id;
        QVERIFY(controller.setPasswordListEnabled(id, false));
        controller.extractDestination = temporary.filePath("disabled-output");
        QVERIFY(controller.start(ArchiveController::Operation::Extract));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 20000);
        QVERIFY(!controller.tasks.last().success); QVERIFY(!QFileInfo::exists(controller.extractDestination));
        QVERIFY(controller.removePasswordList(id)); QCOMPARE(controller.passwordLibrary().lists().size(), 1);
    }
    void matchingCanPauseAndCancel() {
        QTemporaryDir temporary;
        const auto archive = MakeArchive(temporary.path(), "secret", "7z", "unknown-pass", true); QVERIFY(!archive.isEmpty());
        ArchiveController controller(nullptr, temporary.filePath("library.dat")); controller.autoPasswords = true;
        QVERIFY(controller.addPasswordList("list", "one\ntwo\nthree"));
        controller.addPaths({archive}, false); controller.extractDestination = temporary.filePath("cancel-output");
        QVERIFY(controller.start(ArchiveController::Operation::Extract)); controller.togglePause(); QVERIFY(controller.paused());
        const auto before = Read(temporary.filePath("library.dat"));
        QVERIFY(!controller.addPasswords("new")); QVERIFY(!controller.editPassword("one", "edited"));
        QVERIFY(!controller.removePasswords({"one"})); QVERIFY(!controller.setPasswordEnabled("one", false));
        QCOMPARE(Read(temporary.filePath("library.dat")), before);
        QTest::qWait(80); QVERIFY(controller.busy());
        controller.cancel(); QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 20000);
        QVERIFY(!QFileInfo::exists(controller.extractDestination));
    }
    void optionalRarHeadersAndVolumes() {
        const auto archive = QDir::current().absoluteFilePath("rar-fixtures/sample.part2.rar");
        if (!QFileInfo::exists(archive)) QSKIP("Optional locally generated RAR fixture is absent");
        SevenZipCore core;
        const auto result = ResolveArchivePassword(core, archive.toStdWString(), {{"wrong", "rar-list"}, {"test-secret", "rar-list"}}, {});
        QCOMPARE(result.state, ArchivePasswordResult::State::Ready); QCOMPARE(result.attempts, 2); QVERIFY(result.verified);
        core.CloseArchive(); QTemporaryDir temp;
        const auto fixtures = QFileInfo(archive).dir();
        const auto parts = fixtures.entryList({"sample.part*.rar"}, QDir::Files, QDir::Name); QVERIFY(parts.size() > 2);
        QStringList renamed;
        for (int i = 0; i < parts.size(); ++i) {
            const auto path = temp.filePath(QString("p.%1.pdf").arg(i + 1, 3, 10, QChar('0')));
            QVERIFY(QFile::copy(fixtures.filePath(parts[i]), path)); renamed << path;
        }
        const auto disguised = ResolveArchivePassword(core, renamed.last().toStdWString(), {{"wrong", "rar-list"}, {"test-secret", "rar-list"}}, {});
        QCOMPARE(disguised.state, ArchivePasswordResult::State::Ready); QCOMPARE(disguised.attempts, 2); QVERIFY(disguised.verified);
        QVERIFY(core.Extract(temp.filePath("out").toStdWString(), L"test-secret"));
        QCOMPARE(Read(temp.filePath("out/payload.bin")), Read(fixtures.filePath("payload.bin"))); core.CloseArchive();
        QVERIFY(QFile::remove(renamed.last()));
        const auto missing = ResolveArchivePassword(core, renamed.first().toStdWString(), {{"test-secret", "rar-list"}, {"unused", "rar-list"}}, {});
        QCOMPARE(missing.state, ArchivePasswordResult::State::Invalid); QCOMPARE(missing.attempts, 1);
        QVERIFY(missing.error.contains(QStringLiteral("分卷"))); QVERIFY(!core.IsOpen());
    }
};
QTEST_MAIN(PasswordTests)
#include "PasswordTests.moc"
