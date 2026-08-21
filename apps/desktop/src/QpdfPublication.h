#pragma once

#include <QString>

namespace QpdfPublication {
enum class Result { Succeeded, DestinationExists, Failed };

// Publishes a completed same-filesystem staging file without ever replacing an
// existing directory entry. Callers retain ownership of the staging file on
// failure.
Result publishNoReplace(const QString& source, const QString& destination);
}
