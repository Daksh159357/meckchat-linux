#include <iostream>
#include <cassert>
#include <QCoreApplication>
#include <QTimer>
#include <QTemporaryDir>
#include <QFile>
#include <QHostAddress>

#include "meckchat/protocol/models.h"
#include "meckchat/protocol/framing.h"
#include "meckchat/protocol/file_transfer_controller.h"
#include "meckchat/network/p2p_socket.h"
#include "meckchat/core/logger.h"

using namespace MeckChat::Protocol;
using namespace MeckChat::Network;

#define TEST_CHECK(cond) do { \
    if (!(cond)) { \
        std::cerr << "TEST ASSERTION FAILED: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while(0)

void testFileModelsSerialization() {
    std::cout << "[RUN] testFileModelsSerialization" << std::endl;

    // 1. FileOffer
    FileOffer offer;
    offer.transferId = "ft_test_12345678";
    offer.fileName = "report_2026.pdf";
    offer.fileSize = 1048576;
    offer.sha256 = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    offer.chunkSize = 65536;
    offer.totalChunks = 16;

    QJsonObject offerJson = offer.toJson();
    auto decodedOffer = FileOffer::fromJson(offerJson);
    TEST_CHECK(decodedOffer.has_value());
    TEST_CHECK(decodedOffer->transferId == offer.transferId);
    TEST_CHECK(decodedOffer->fileName == offer.fileName);
    TEST_CHECK(decodedOffer->fileSize == offer.fileSize);
    TEST_CHECK(decodedOffer->sha256 == offer.sha256);
    TEST_CHECK(decodedOffer->chunkSize == 65536);
    TEST_CHECK(decodedOffer->totalChunks == 16);

    // 2. FileAccept
    FileAccept acc;
    acc.transferId = "ft_test_12345678";
    auto decodedAcc = FileAccept::fromJson(acc.toJson());
    TEST_CHECK(decodedAcc.has_value());
    TEST_CHECK(decodedAcc->transferId == "ft_test_12345678");

    // 3. FileReject
    FileReject rej;
    rej.transferId = "ft_test_12345678";
    rej.reason = "user_declined";
    auto decodedRej = FileReject::fromJson(rej.toJson());
    TEST_CHECK(decodedRej.has_value());
    TEST_CHECK(decodedRej->transferId == "ft_test_12345678");
    TEST_CHECK(decodedRej->reason == "user_declined");

    // 4. FileComplete
    FileComplete comp;
    comp.transferId = "ft_test_12345678";
    comp.sha256 = offer.sha256;
    comp.status = "verified";
    auto decodedComp = FileComplete::fromJson(comp.toJson());
    TEST_CHECK(decodedComp.has_value());
    TEST_CHECK(decodedComp->transferId == "ft_test_12345678");
    TEST_CHECK(decodedComp->sha256 == offer.sha256);
    TEST_CHECK(decodedComp->status == "verified");

    // 5. FileCancel
    FileCancel can;
    can.transferId = "ft_test_12345678";
    can.reason = "network_timeout";
    auto decodedCan = FileCancel::fromJson(can.toJson());
    TEST_CHECK(decodedCan.has_value());
    TEST_CHECK(decodedCan->transferId == "ft_test_12345678");
    TEST_CHECK(decodedCan->reason == "network_timeout");

    // 6. Invalid JSON payload
    QJsonObject invalidJson;
    invalidJson["invalid_field"] = "123";
    TEST_CHECK(!FileOffer::fromJson(invalidJson).has_value());
    TEST_CHECK(!FileAccept::fromJson(invalidJson).has_value());
    TEST_CHECK(!FileReject::fromJson(invalidJson).has_value());
    TEST_CHECK(!FileComplete::fromJson(invalidJson).has_value());
    TEST_CHECK(!FileCancel::fromJson(invalidJson).has_value());

    std::cout << "[PASS] testFileModelsSerialization" << std::endl;
}

void testFileChunkCodec() {
    std::cout << "[RUN] testFileChunkCodec" << std::endl;

    QString tid = "ft_1a2b3c4d5e6f";
    uint32_t chunkIdx = 7;
    QByteArray chunkData = "MeckChat 64 KiB binary chunk stream test payload with bytes \x00\x01\x02\xFF!";

    QByteArray encoded = FileChunk::encode(tid, chunkIdx, chunkData);
    TEST_CHECK(encoded.size() == 24 + chunkData.size());

    QString decodedTid;
    uint32_t decodedIdx = 0;
    QByteArray decodedData;

    bool ok = FileChunk::decode(encoded, decodedTid, decodedIdx, decodedData);
    TEST_CHECK(ok);
    TEST_CHECK(decodedTid == tid);
    TEST_CHECK(decodedIdx == chunkIdx);
    TEST_CHECK(decodedData == chunkData);

    // Truncated packet
    QByteArray truncated = encoded.left(20);
    TEST_CHECK(!FileChunk::decode(truncated, decodedTid, decodedIdx, decodedData));

    std::cout << "[PASS] testFileChunkCodec" << std::endl;
}

void testFilenameSanitization() {
    std::cout << "[RUN] testFilenameSanitization" << std::endl;

    TEST_CHECK(sanitizeFileName("document.pdf") == "document.pdf");
    TEST_CHECK(sanitizeFileName("../../../etc/passwd") == "passwd");
    TEST_CHECK(sanitizeFileName("/home/user/secret.key") == "secret.key");
    TEST_CHECK(sanitizeFileName("C:\\Windows\\System32\\cmd.exe") == "cmd.exe");
    TEST_CHECK(sanitizeFileName("..") == "received_file");
    TEST_CHECK(sanitizeFileName("") == "received_file");

    std::cout << "[PASS] testFilenameSanitization" << std::endl;
}

void testStreamingSha256Verification() {
    std::cout << "[RUN] testStreamingSha256Verification" << std::endl;

    QTemporaryDir tempDir;
    TEST_CHECK(tempDir.isValid());

    QString filePath = tempDir.filePath("test_data.bin");
    QFile file(filePath);
    TEST_CHECK(file.open(QIODevice::WriteOnly));

    QByteArray testData(131072, 'K'); // 128 KiB
    file.write(testData);
    file.close();

    QString hash = FileTransferController::calculateFileSha256(filePath);
    TEST_CHECK(!hash.isEmpty());

    QCryptographicHash stdHash(QCryptographicHash::Sha256);
    stdHash.addData(testData);
    TEST_CHECK(hash == stdHash.result().toHex());

    std::cout << "[PASS] testStreamingSha256Verification" << std::endl;
}

void testBoundaryAndValidationScenarios() {
    std::cout << "[RUN] testBoundaryAndValidationScenarios" << std::endl;

    QTemporaryDir tempDir;
    TEST_CHECK(tempDir.isValid());

    // 1. Empty (0-byte) file SHA-256 test
    QString emptyPath = tempDir.filePath("empty.bin");
    QFile emptyFile(emptyPath);
    TEST_CHECK(emptyFile.open(QIODevice::WriteOnly));
    emptyFile.close();
    QString emptySha256 = FileTransferController::calculateFileSha256(emptyPath);
    TEST_CHECK(emptySha256 == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

    // 2. Exactly 64 KiB boundary
    QString exact64kPath = tempDir.filePath("exact64k.bin");
    QFile exact64kFile(exact64kPath);
    TEST_CHECK(exact64kFile.open(QIODevice::WriteOnly));
    exact64kFile.write(QByteArray(65536, 'A'));
    exact64kFile.close();
    QString exact64kHash = FileTransferController::calculateFileSha256(exact64kPath);
    TEST_CHECK(!exact64kHash.isEmpty());

    // 3. 64 KiB + 1 byte boundary
    QString plusOnePath = tempDir.filePath("plus_one.bin");
    QFile plusOneFile(plusOnePath);
    TEST_CHECK(plusOneFile.open(QIODevice::WriteOnly));
    plusOneFile.write(QByteArray(65537, 'B'));
    plusOneFile.close();
    QString plusOneHash = FileTransferController::calculateFileSha256(plusOnePath);
    TEST_CHECK(!plusOneHash.isEmpty() && plusOneHash != exact64kHash);

    // 4. Unicode and Special Character Filename sanitization
    TEST_CHECK(sanitizeFileName("档案_файл_📄_2026.pdf") == "档案_файл_📄_2026.pdf");
    TEST_CHECK(sanitizeFileName("space filename with (parens) [brackets].tar.gz") == "space filename with (parens) [brackets].tar.gz");
    TEST_CHECK(sanitizeFileName("../../../../../etc/shadow") == "shadow");

    // 5. Large File (1 MiB) Streaming SHA-256 Test
    QString largePath = tempDir.filePath("1mb_test.bin");
    QFile largeFile(largePath);
    TEST_CHECK(largeFile.open(QIODevice::WriteOnly));
    for (int i = 0; i < 16; ++i) {
        largeFile.write(QByteArray(65536, static_cast<char>('A' + i)));
    }
    largeFile.close();
    QString largeHash = FileTransferController::calculateFileSha256(largePath);
    TEST_CHECK(!largeHash.isEmpty());

    std::cout << "[PASS] testBoundaryAndValidationScenarios" << std::endl;
}

void testEndToEndP2PFileTransfer() {
    std::cout << "[RUN] testEndToEndP2PFileTransfer (Localhost Port 17790)" << std::endl;

    quint16 testPort = 17790;
    QTemporaryDir senderDir;
    QTemporaryDir receiverDir;
    TEST_CHECK(senderDir.isValid() && receiverDir.isValid());

    // 1. Create a 200 KiB test source file (crossing 64 KiB chunks)
    QString srcFile = senderDir.filePath("large_transfer_test.dat");
    QFile f(srcFile);
    TEST_CHECK(f.open(QIODevice::WriteOnly));
    QByteArray sampleData(204800, 'Z'); // 200 KiB (4 chunks: 64k + 64k + 64k + 8k)
    f.write(sampleData);
    f.close();

    QString expectedSha256 = FileTransferController::calculateFileSha256(srcFile);

    P2PSocket serverSocket;
    P2PSocket clientSocket;

    FileTransferController serverFT(&serverSocket);
    FileTransferController clientFT(&clientSocket);

    bool offerReceived = false;
    bool transferFinished = false;
    QString completedPath;

    // Start Server
    bool serverStarted = serverSocket.startServer(testPort, QHostAddress("127.0.0.1"));
    TEST_CHECK(serverStarted);

    // Client connects
    clientSocket.connectToPeer("127.0.0.1", testPort);

    // Receiver (Server) signals
    QObject::connect(&serverFT, &FileTransferController::fileOfferReceived, [&](const FileOffer &offer) {
        offerReceived = true;
        std::cout << "  -> Receiver received FileOffer: " << offer.fileName.toStdString() << " (" << offer.fileSize << " bytes)" << std::endl;
        TEST_CHECK(offer.fileName == "large_transfer_test.dat");
        TEST_CHECK(offer.fileSize == 204800);

        // Accept offer and save to receiverDir
        serverFT.acceptTransfer(offer.transferId, receiverDir.path());
    });

    QObject::connect(&serverFT, &FileTransferController::transferCompleted, [&](const QString &tid, const QString &finalPath) {
        transferFinished = true;
        completedPath = finalPath;
        std::cout << "  -> Receiver completed file transfer: " << tid.toStdString() << " -> " << finalPath.toStdString() << std::endl;

        // Verify received file matches source
        QFile recFile(finalPath);
        TEST_CHECK(recFile.open(QIODevice::ReadOnly));
        QByteArray recData = recFile.readAll();
        recFile.close();
        TEST_CHECK(recData == sampleData);

        // Disconnect and quit
        clientSocket.disconnectFromPeer();
        serverSocket.stopServer();
        QTimer::singleShot(20, []() {
            QCoreApplication::quit();
        });
    });

    // Sender (Client) signals
    QObject::connect(&clientSocket, &P2PSocket::connected, [&]() {
        std::cout << "  -> Client connected. Initiating file transfer..." << std::endl;
        QTimer::singleShot(50, [&]() {
            clientFT.sendFile(srcFile);
        });
    });

    // Timeout safety guard (5s)
    QTimer::singleShot(5000, [&]() {
        if (!transferFinished) {
            std::cerr << "  -> File transfer test timed out!" << std::endl;
            QCoreApplication::exit(1);
        }
    });

    int ret = QCoreApplication::exec();
    TEST_CHECK(ret == 0);
    TEST_CHECK(offerReceived);
    TEST_CHECK(transferFinished);
    TEST_CHECK(FileTransferController::calculateFileSha256(completedPath) == expectedSha256);

    std::cout << "[PASS] testEndToEndP2PFileTransfer" << std::endl;
}

void testFileTransferCancellation() {
    std::cout << "[RUN] testFileTransferCancellation (Localhost Port 17791)" << std::endl;

    quint16 testPort = 17791;
    QTemporaryDir senderDir;
    QTemporaryDir receiverDir;

    QString srcFile = senderDir.filePath("cancel_test.dat");
    QFile f(srcFile);
    TEST_CHECK(f.open(QIODevice::WriteOnly));
    f.write(QByteArray(131072, 'X')); // 128 KiB
    f.close();

    P2PSocket serverSocket;
    P2PSocket clientSocket;

    FileTransferController serverFT(&serverSocket);
    FileTransferController clientFT(&clientSocket);

    bool cancelObserved = false;
    QString activeTid;

    serverSocket.startServer(testPort, QHostAddress("127.0.0.1"));
    clientSocket.connectToPeer("127.0.0.1", testPort);

    QObject::connect(&serverFT, &FileTransferController::fileOfferReceived, [&](const FileOffer &offer) {
        activeTid = offer.transferId;
        serverFT.acceptTransfer(offer.transferId, receiverDir.path());

        // Cancel from sender side immediately after acceptance
        QTimer::singleShot(10, [&]() {
            clientFT.cancelTransfer(activeTid, "cancelled_by_sender_test");
        });
    });

    QObject::connect(&serverFT, &FileTransferController::transferCancelled, [&](const QString &tid, const QString &reason) {
        cancelObserved = true;
        std::cout << "  -> Receiver observed transfer cancellation: " << tid.toStdString() << " (Reason: " << reason.toStdString() << ")" << std::endl;

        clientSocket.disconnectFromPeer();
        serverSocket.stopServer();
        QTimer::singleShot(20, []() {
            QCoreApplication::quit();
        });
    });

    QObject::connect(&clientSocket, &P2PSocket::connected, [&]() {
        QTimer::singleShot(50, [&]() {
            clientFT.sendFile(srcFile);
        });
    });

    QTimer::singleShot(5000, [&]() {
        if (!cancelObserved) {
            std::cerr << "  -> Cancel test timed out!" << std::endl;
            QCoreApplication::exit(1);
        }
    });

    int ret = QCoreApplication::exec();
    TEST_CHECK(ret == 0);
    TEST_CHECK(cancelObserved);

    std::cout << "[PASS] testFileTransferCancellation" << std::endl;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    MeckChat::Core::Logger::init();

    testFileModelsSerialization();
    testFileChunkCodec();
    testFilenameSanitization();
    testStreamingSha256Verification();
    testBoundaryAndValidationScenarios();
    testEndToEndP2PFileTransfer();
    testFileTransferCancellation();

    std::cout << "\n==============================================" << std::endl;
    std::cout << "  All P2P File Transfer Protocol Tests Passed! " << std::endl;
    std::cout << "==============================================" << std::endl;
    return 0;
}
