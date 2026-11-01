#pragma once
#include "ArchiveController.h"
#include <QCoreApplication>

inline void ConnectApplicationExit(ArchiveController& controller, QCoreApplication& application) {
    QObject::connect(&controller, &ArchiveController::quitReady, &application, [] {
        // Explicit shutdown must bypass windows that intentionally hide on close.
        QCoreApplication::exit(0);
    });
}
