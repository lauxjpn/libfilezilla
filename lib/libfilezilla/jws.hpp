#ifndef LIBFILEZILLA_JWS_HEADER
#define LIBFILEZILLA_JWS_HEADER

#include "json.hpp"

namespace fz {

// priv and pub
std::pair<json, json> FZ_PUBLIC_SYMBOL create_jwk();

json FZ_PUBLIC_SYMBOL jws_sign_flattened(json const& priv, json const& payload, json const& extra_protected = {});
}

#endif
