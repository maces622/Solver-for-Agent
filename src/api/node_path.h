#ifndef GTO_API_NODE_PATH_H
#define GTO_API_NODE_PATH_H

#include <QString>

namespace GtoApi {

// Validates the transport-level shape of a deterministic tree node id:
// "root" or "root/" followed by non-empty aN or c<card> segments.
// Tree-dependent validity (child indexes and card availability) is checked by
// the node resolver once a GameTree is available.
bool isValidNodePathSyntax(const QString &nodeId, QString *errorMessage = nullptr);

} // namespace GtoApi

#endif // GTO_API_NODE_PATH_H
