#ifndef LIBFILEZILLA_TLS_LAYER_IMPL_HEADER
#define LIBFILEZILLA_TLS_LAYER_IMPL_HEADER

#if defined(_MSC_VER)
typedef std::make_signed_t<size_t> ssize_t;
#endif

#include <gnutls/gnutls.h>

#include "libfilezilla/buffer.hpp"
#include "libfilezilla/logger.hpp"
#include "libfilezilla/socket.hpp"
#include "libfilezilla/tls_info.hpp"

namespace fz {
class tls_system_trust_store;
class logger_interface;

class tls_layer;
class tls_layer_impl final
{
public:
	tls_layer_impl(tls_layer& layer, tls_system_trust_store * systemTrustStore, logger_interface & logger);
	~tls_layer_impl();

	bool client_handshake(std::vector<uint8_t> const& session_to_resume, native_string const& session_hostname, std::vector<uint8_t> const& required_certificate, event_handler * verification_handler);

	bool server_handshake(std::vector<uint8_t> const& session_to_resume);

	int connect(native_string const& host, unsigned int port, address_type family);

	int read(void *buffer, unsigned int size, int& error);
	int write(void const* buffer, unsigned int size, int& error);

	int shutdown();

	void set_verification_result(bool trusted);

	socket_state get_state() const {
		return state_;
	}

	std::vector<uint8_t> get_session_parameters() const;
	std::vector<uint8_t> get_raw_certificate() const;

	std::string get_protocol() const;
	std::string get_key_exchange() const;
	std::string get_cipher() const;
	std::string get_mac() const;
	int get_algorithm_warnings() const;

	bool resumed_session() const;

	static std::string list_tls_ciphers(std::string const& priority);

	bool set_certificate_file(native_string const& keyfile, native_string const& certsfile, native_string const& password, bool pem);

	bool set_certificate(std::string const& key, std::string const& certs, native_string const& password, bool pem);

	static std::string get_gnutls_version();

	ssize_t push_function(void const* data, size_t len);
	ssize_t pull_function(void* data, size_t len);

	static std::pair<std::string, std::string> generate_selfsigned_certificate(native_string const& password, std::string const& distinguished_name, std::vector<std::string> const& hostnames);

	int shutdown_read();

private:
	bool init();
	void deinit();

	bool init_session(bool client);
	void deinit_session();

	int continue_write();
	int continue_handshake();
	int continue_shutdown();

	int verify_certificate();
	bool certificate_is_blacklisted(std::vector<x509_certificate> const& certificates);

	void log_error(int code, std::wstring const& function, logmsg::type logLevel = logmsg::error);
	void log_alert(logmsg::type logLevel);

	// Failure logs the error, uninits the session and sends a close event
	void failure(int code, bool send_close, std::wstring const& function = std::wstring());

	int do_call_gnutls_record_recv(void* data, size_t len);

	void operator()(event_base const& ev);
	void on_socket_event(socket_event_source* source, socket_event_flag t, int error);
	void forward_hostaddress_event(socket_event_source* source, std::string const& address);

	void on_read();
	void on_send();

	bool get_sorted_peer_certificates(gnutls_x509_crt_t *& certs, unsigned int & certs_size);

	bool extract_cert(gnutls_x509_crt_t const& cert, x509_certificate& out);
	std::vector<x509_certificate::SubjectName> get_cert_subject_alt_names(gnutls_x509_crt_t cert);

	void log_verification_error(int status);

	void set_hostname(native_string const& host);

	bool is_client() const {
		return ticket_key_.empty();
	}

	tls_layer& tls_layer_;

	socket_state state_{};

	logger_interface & logger_;

	bool initialized_{};

	gnutls_session_t session_{};

	std::vector<uint8_t> ticket_key_;

	gnutls_certificate_credentials_t cert_credentials_{};
	bool handshake_successful_{};
	bool sent_closure_alert_{};

	bool can_read_from_socket_{false};
	bool can_write_to_socket_{false};

	bool shutdown_silence_read_errors_{true};

	// gnutls_record_send has strange semantics, it needs to be called again
	// with either 0 data and 0 length after GNUTLS_E_AGAIN, to actually send
	// previously queued data. We unfortunately do not know how much data has
	// been queued and thus need to make a copy of the input up to
	// gnutls_record_get_max_size()
	buffer send_buffer_;

	std::vector<uint8_t> required_certificate_;

	bool socket_eof_{};
	int socket_error_{ECONNABORTED}; // Set in the push and pull functions if reading/writing fails fatally

	friend class tls_layer;
	friend class tls_layerCallbacks;

	native_string hostname_;

	tls_system_trust_store* system_trust_store_{};

	event_handler * verification_handler_{};
};
}

#endif
