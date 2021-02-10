#ifndef LIBFILEZILLA_TLS_LAYER_HEADER
#define LIBFILEZILLA_TLS_LAYER_HEADER

/** \file
 * \brief A Transport Layer Security (TLS) layer
 */

#include "socket.hpp"

namespace fz {
class logger_interface;
class tls_system_trust_store;
class tls_session_info;

class tls_layer;
class tls_layer_impl;

struct certificate_verification_event_type;

/** \brief This event gets sent during the handshake with details about the session and the used certificate
 *
 * After receiving this event, \ref tls_layer::set_verification_result needs to be called eventually.
 */
typedef simple_event<certificate_verification_event_type, tls_layer*, tls_session_info> certificate_verification_event;

/**
 * \brief A Transport Layer Security (TLS) layer
 *
 * Can be used both for client- and server-side TLS.
 *
 * This class also supports TLS session resumption. Resumption has to be requested
 * explicitly, there is no shared state between unrelated sessions.
 *
 * Two trust models are possible with this class for client-side TLS: Certificates
 * can either be evaluated against the system trust store, or you can implement a
 * custom trust model such as TOFU.
 */
class FZ_PUBLIC_SYMBOL tls_layer final : protected event_handler, public socket_layer
{
public:
	tls_layer(event_loop& event_loop, event_handler* evt_handler, socket_interface& layer, tls_system_trust_store * system_trust_store, logger_interface& logger);
	virtual ~tls_layer();

	/**
	 * \brief Starts shaking hands for a new TLS session as client.
	 *
	 * Returns true if the handshake has started, false on error.
	 *
	 * If the handshake is started, wait for a connection event for the result.
	 *
	 * The certificate that eventually gets negotiated for the session
	 * must match the passed \c required_certificate, either in DER or PEM,
	 * or the handshake will fail.
	 */
	bool client_handshake(std::vector<uint8_t> const& required_certificate, std::vector<uint8_t> const& session_to_resume = std::vector<uint8_t>(), native_string const& session_hostname = native_string());

	/**
	 * \brief Starts shaking hands for a new TLS session as client.
	 *
	 * Returns true if the handshake has started, false on error.
	 *
	 * If the handshake is started, wait for a connection event for the result.
	 *
	 * If no verification handler is passed, verification is done solely using the system
	 * trust store.
	 *
	 * If a verification handler is passed, it will receive a
	 * \ref certificate_verification_event event upon which the handshake is paused until
	 * \ref set_verification_result gets called including a \ref tls_session_info structure.
	 * The handler is called even for certificates not trusted by the system trust store, allowing
	 * the following impairments: Unknown issuer, wrong hostname and certificates used outside their validity time.
	 */
	bool client_handshake(event_handler *const verification_handler, std::vector<uint8_t> const& session_to_resume = std::vector<uint8_t>(), native_string const& session_hostname = native_string());

	/**
	 * \brief Starts shaking hand for a new TLS session as server.
	 *
	 * Returns true if the handshake has started, false on error.
	 *
	 * If the handshake is started, wait for a connection event for the result.
	 *
	 * Before calling server_handshake, a valid certificate and key must be passed
	 * in through \ref set_certificate.
	 */
	bool server_handshake();

	/**
	 * \brief Starts shaking hand for a new TLS session as server.
	 *
	 * Returns true if the handshake has started, false on error.
	 *
	 * If the handshake is started, wait for a connection event for the result.
	 *
	 * Before calling server_handshake, a valid certificate and key must be passed
	 * in through \ref set_certificate.
	 * The preamble is sent out after setting up all the parameters, but before the first handshake message
	 */
	bool server_handshake(std::vector<uint8_t> const& session_to_resume, std::string_view const& preamble = {});

	/// Gets session parameters for resumption
	std::vector<uint8_t> get_session_parameters() const;

	/// Gets the session's peer certificate in DER
	std::vector<uint8_t> get_raw_certificate() const;

	/**
	 * \brief Must be called after having received certificate_verification_event
	 *
	 * Can be used to trust a certificate even if it is not trusted via the system trust store.
	 */
	void set_verification_result(bool trusted);

	std::string get_protocol() const;

	std::string get_key_exchange() const;
	std::string get_cipher() const;
	std::string get_mac() const;
	int get_algorithm_warnings() const;

	/// After a successful handshake, returns whether the session has been resumed.
	bool resumed_session() const;

	/// Returns a human-readable list of all TLS ciphers available with the passed priority string
	static std::string list_tls_ciphers(std::string const& priority);

	/** \brief Sets the file containing the certificate (and its chain) and the file with the corresponding private key.
	 *
	 * For servers a certificate is mandatory, it is presented to the client during the handshake.
	 *
	 * For clients it is the optional client certificate.
	 *
	 * If the pem flag is set, the input is assumed to be in PEM, otherwise DER.
	 */
	bool set_certificate_file(native_string const& keyfile, native_string const& certsfile, native_string const& password, bool pem = true);

	/** \brief Sets the certificate (and its chain) and the private key
	 *
	 * For servers it is mandatory and is the certificate the server presents to the client.
	 *
	 * For clients it is the optional client certificate.
	 *
	 * If the pem flag is set, the input is assumed to be in PEM, otherwise DER.
	 */
	bool set_certificate(std::string const& key, std::string const& certs, native_string const& password, bool pem = true);

	/// Returns the version of the loaded GnuTLS library, may be different than the version used at compile-time.
	static std::string get_gnutls_version();

	/** \brief Creates a new private key and a self-signed certificate.
	 *
	 * The distinguished name must be a RFC4514-compliant string.
	 *
	 * If the password is non-empty, the private key gets encrypted using it.
	 *
	 * The output pair is in PEM, first element is the key and the second the certificate.
	 */
	static std::pair<std::string, std::string> generate_selfsigned_certificate(native_string const& password, std::string const& distinguished_name, std::vector<std::string> const& hostnames);


	virtual socket_state get_state() const override;

	virtual int connect(native_string const& host, unsigned int port, address_type family = address_type::unknown) override;

	virtual int read(void *buffer, unsigned int size, int& error) override;
	virtual int write(void const* buffer, unsigned int size, int& error) override;

	virtual int shutdown() override;

	virtual int shutdown_read() override;

	virtual void set_event_handler(event_handler* pEvtHandler, fz::socket_event_flag retrigger_block = socket_event_flag{}) override;

private:
	virtual void FZ_PRIVATE_SYMBOL operator()(event_base const& ev) override;

	friend class tls_layer_impl;
	std::unique_ptr<tls_layer_impl> impl_;
};
}

#endif
