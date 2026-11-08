#include "ssl_wrapper.h"

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/bio.h>

#include <string>
#include <sstream>
#include <vector>
#include <unordered_map>
#include <cstring>

#include <brotli/decode.h>
#include <brotli/encode.h>

struct PmsSslConnection
{
    SSL_CTX* ctx = nullptr;
    SSL* ssl = nullptr;
    std::string hostname;
};

static thread_local std::string g_last_error;

static void SetLastError(const char* message)
{
    g_last_error = message ? message : "Unknown error";
}

static void SetOpenSslError(const char* prefix)
{
    unsigned long error = ERR_get_error();

    if (error == 0)
    {
        g_last_error = prefix ? prefix : "OpenSSL error";
        return;
    }

    char error_string[256] = {};

    ERR_error_string_n(
        error,
        error_string,
        sizeof(error_string));

    g_last_error =
        std::string(prefix ? prefix : "OpenSSL error") +
        ": " +
        error_string;
}

static int TranslateSslError(
    SSL* ssl,
    int result)
{
    int error = SSL_get_error(ssl, result);

    switch (error)
    {
        case SSL_ERROR_NONE:
            return PMS_SSL_OK;

        case SSL_ERROR_ZERO_RETURN:
            return PMS_SSL_ZERO_RETURN;

        case SSL_ERROR_WANT_READ:
            return PMS_SSL_WANT_READ;

        case SSL_ERROR_WANT_WRITE:
            return PMS_SSL_WANT_WRITE;

        default:
            SetOpenSslError("SSL error");
            return PMS_SSL_ERROR;
    }
}

static std::vector<std::string> ParseProtos(const char* protos_str)
{
    std::vector<std::string> result;

    if (!protos_str || !protos_str[0]) {
        return result;
    }

    std::string input(protos_str);
    std::stringstream ss(input);
    std::string item;

    while (std::getline(ss, item, ':'))
    {
        if (!item.empty() && item.length() <= 255)
        {
            result.push_back(item);
        }
    }

    return result;
}

static std::vector<uint16_t> ParseSignatureAlgorithms(const char* sigalgs_str)
{
    std::vector<uint16_t> result;
    if (!sigalgs_str || !sigalgs_str[0]) {
        return result;
    }

    // boringssl/include/openssl/ssl.h

    static const std::unordered_map<std::string, uint16_t> sigalgs_map = {
        { "RSA-PKCS1-SHA1", static_cast<uint16_t>(SSL_SIGN_RSA_PKCS1_SHA1) },
        { "RSA-PKCS1-SHA256", static_cast<uint16_t>(SSL_SIGN_RSA_PKCS1_SHA256) },
        { "RSA-PKCS1-SHA384", static_cast<uint16_t>(SSL_SIGN_RSA_PKCS1_SHA384) },
        { "RSA-PKCS1-SHA512", static_cast<uint16_t>(SSL_SIGN_RSA_PKCS1_SHA512) },
        { "ECDSA-SHA1", static_cast<uint16_t>(SSL_SIGN_ECDSA_SHA1) },
        { "ECDSA-SECP256R1-SHA256", static_cast<uint16_t>(SSL_SIGN_ECDSA_SECP256R1_SHA256) },
        { "ECDSA-SECP384R1-SHA384", static_cast<uint16_t>(SSL_SIGN_ECDSA_SECP384R1_SHA384) },
        { "ECDSA-SECP521R1-SHA512", static_cast<uint16_t>(SSL_SIGN_ECDSA_SECP521R1_SHA512) },
        { "RSA-PSS-RSAE-SHA256", static_cast<uint16_t>(SSL_SIGN_RSA_PSS_RSAE_SHA256) },
        { "RSA-PSS-RSAE-SHA384", static_cast<uint16_t>(SSL_SIGN_RSA_PSS_RSAE_SHA384) },
        { "RSA-PSS-RSAE-SHA512", static_cast<uint16_t>(SSL_SIGN_RSA_PSS_RSAE_SHA512) },
        { "ED25519", static_cast<uint16_t>(SSL_SIGN_ED25519) },
        { "ML-DSA-44", static_cast<uint16_t>(SSL_SIGN_ML_DSA_44) },
        { "ML-DSA-65", static_cast<uint16_t>(SSL_SIGN_ML_DSA_65) },
        { "ML-DSA-87", static_cast<uint16_t>(SSL_SIGN_ML_DSA_87) },
        { "RSA-PKCS1-SHA256-LEGACY", static_cast<uint16_t>(SSL_SIGN_RSA_PKCS1_SHA256_LEGACY) },
        { "RSA-PKCS1-MD5-SHA1", static_cast<uint16_t>(SSL_SIGN_RSA_PKCS1_MD5_SHA1) }
    };

    std::string input(sigalgs_str);
    std::stringstream ss(input);
    std::string item;

    while (std::getline(ss, item, ':'))
    {
        if (!item.empty())
        {
            auto it = sigalgs_map.find(item);
            if (it != sigalgs_map.end())
            {
                result.push_back(it->second);
            }
        }
    }
    return result;
}

static int CompressBrotli(SSL*, CBB *out, const uint8_t *in, size_t in_len)
{
    size_t max_compressed_len = BrotliEncoderMaxCompressedSize(in_len);
    uint8_t *write_ptr;

    if (!CBB_reserve(out, &write_ptr, max_compressed_len)) {
        return 0;
    }

    size_t compressed_len = max_compressed_len;
    
    if (BrotliEncoderCompress(BROTLI_DEFAULT_QUALITY, BROTLI_DEFAULT_WINDOW,
                              BROTLI_DEFAULT_MODE, in_len, in,
                              &compressed_len, write_ptr) != BROTLI_TRUE)
    {
        return 0;
    }

    if (!CBB_did_write(out, compressed_len)) {
        return 0;
    }

    return 1;
}

static int DecompressBrotli(SSL*, CRYPTO_BUFFER **out, size_t uncompressed_len,
                            const uint8_t *in, size_t in_len)
{
    uint8_t *out_data;
    CRYPTO_BUFFER *buf = CRYPTO_BUFFER_alloc(&out_data, uncompressed_len);
    if (buf == nullptr) {
        return 0;
    }

    size_t decoded_len = uncompressed_len;
    BrotliDecoderResult result = BrotliDecoderDecompress(
        in_len, in, &decoded_len, out_data);

    if (result != BROTLI_DECODER_RESULT_SUCCESS || decoded_len != uncompressed_len)
    {
        CRYPTO_BUFFER_free(buf);
        return 0;
    }

    *out = buf;
    return 1;
}

extern "C"
{

PMS_EXPORT void* PMS_CALL pms_ssl_create(
    const char* hostname,
    const char* cipher_list,
    int enable_grease,
    int enable_ech_grease,
    const char* alpn_protos,
    const char* alps_protos,
    const char* trust_anchors,
    const char* sig_algs,
    int enable_signed_cert_timestamps,
    int set_ocsp_status_type,
    int enable_brotli)
{
    if (!hostname || !hostname[0])
    {
        SetLastError("Hostname is empty");
        return nullptr;
    }

    auto* connection = new PmsSslConnection();

    connection->hostname = hostname;

    connection->ctx = SSL_CTX_new(TLS_client_method());

    if (!connection->ctx)
    {
        SetOpenSslError("SSL_CTX_new failed");
        delete connection;
        return nullptr;
    }

    // POC only.
    // Certificate verification should be enabled later.
    SSL_CTX_set_verify(
        connection->ctx,
        SSL_VERIFY_NONE,
        nullptr);

    if (enable_grease) 
    {
        SSL_CTX_set_grease_enabled(connection->ctx, 1);
        SSL_CTX_set_grease_sigalgs_enabled(connection->ctx, 1);
        SSL_CTX_set_permute_extensions(connection->ctx, 1);
    }

    if (cipher_list && cipher_list[0])
    {
        if (SSL_CTX_set_cipher_list(connection->ctx, cipher_list) != 1)
        {
            SetOpenSslError("SSL_CTX_set_cipher_list failed");
            SSL_CTX_free(connection->ctx);
            delete connection;
            return nullptr;
        }
    }

    std::vector<std::string> alpn_protocols = ParseProtos(alpn_protos);

    if (!alpn_protocols.empty())
    {
        std::vector<uint8_t> alpn_wire_format;
        for (const auto& proto : alpn_protocols)
        {
            alpn_wire_format.push_back(static_cast<uint8_t>(proto.length()));
            alpn_wire_format.insert(alpn_wire_format.end(), proto.begin(), proto.end());
        }

        if (SSL_CTX_set_alpn_protos(
            connection->ctx, 
            alpn_wire_format.data(), 
            static_cast<unsigned>(alpn_wire_format.size())) != 0)
        {
            SetOpenSslError("SSL_CTX_set_alpn_protos failed");
            SSL_CTX_free(connection->ctx);
            delete connection;
            return nullptr;
        }
    }

    if (enable_signed_cert_timestamps) 
    {
        SSL_CTX_enable_signed_cert_timestamps(connection->ctx);
    }

    if (trust_anchors && trust_anchors[0])
    {
        size_t len = strlen(trust_anchors);

        std::vector<uint8_t> ta_ids; 
        ta_ids.reserve(len / 2);

        for (size_t i = 0; i < len; i += 2) {
            char byte_chars[3] = { trust_anchors[i], trust_anchors[i + 1], '\0' };
            uint8_t byte = static_cast<uint8_t>(strtoul(byte_chars, nullptr, 16));
            ta_ids.push_back(byte);
        }

        if (SSL_CTX_set1_requested_trust_anchors(
            connection->ctx, 
            ta_ids.data(), 
            static_cast<unsigned>(ta_ids.size())) != 1)
        {
            SetOpenSslError("SSL_CTX_set1_requested_trust_anchors failed");
            SSL_CTX_free(connection->ctx);
            delete connection;
            return nullptr;        
        }
    }

    std::vector<uint16_t> sigalgs = ParseSignatureAlgorithms(sig_algs);

    if (enable_brotli)
    {
        if (SSL_CTX_add_cert_compression_alg(
            connection->ctx, 
            2, // TLSEXT_cert_compression_brotli
            CompressBrotli, 
            DecompressBrotli) != 1)
        {
            SetOpenSslError("Failed to enable built-in Brotli compression");
            SSL_CTX_free(connection->ctx);
            delete connection;
            return nullptr;
        }
    }

    connection->ssl = SSL_new(connection->ctx);

    if (!connection->ssl)
    {
        SetOpenSslError("SSL_new failed");
        SSL_CTX_free(connection->ctx);
        delete connection;
        return nullptr;
    }

    if (enable_ech_grease) 
    {
        SSL_set_enable_ech_grease(connection->ssl, 1);
    }

    if (set_ocsp_status_type)
    {
        if (SSL_set_tlsext_status_type(connection->ssl, TLSEXT_STATUSTYPE_ocsp) != 1)
        {
            SetOpenSslError("SSL_set_tlsext_status_type failed");
            SSL_free(connection->ssl);
            SSL_CTX_free(connection->ctx);
            delete connection;
            return nullptr;
        }    
    }

    std::vector<std::string> alps_protocols = ParseProtos(alps_protos);

    if (!alps_protocols.empty())
    {
        const uint8_t empty_settings[] = { 0 };

        for (const auto& proto : alps_protocols)
        {
            if (SSL_add_application_settings(
                    connection->ssl, 
                    reinterpret_cast<const uint8_t*>(proto.data()), 
                    static_cast<int>(proto.length()), 
                    empty_settings, 0) != 1)
            {
                SetOpenSslError("SSL_add_application_settings failed");
                SSL_free(connection->ssl);
                SSL_CTX_free(connection->ctx);
                delete connection;
                return nullptr;            
            }
        }
    }

    if (!sigalgs.empty())
    {
        if (SSL_set_verify_algorithm_prefs(
            connection->ssl, 
            sigalgs.data(), 
            sigalgs.size()) != 1)
        {
            SetOpenSslError("SSL_set_verify_algorithm_prefs failed");
            SSL_free(connection->ssl);
            SSL_CTX_free(connection->ctx);
            delete connection;
            return nullptr;
        }

        if (SSL_set_signing_algorithm_prefs(
            connection->ssl, 
            sigalgs.data(), 
            sigalgs.size()) != 1)
        {
            SetOpenSslError("SSL_set_signing_algorithm_prefs failed");
            SSL_free(connection->ssl);
            SSL_CTX_free(connection->ctx);
            delete connection;
            return nullptr;
        }
    }    

    BIO* read_bio = BIO_new(BIO_s_mem());
    BIO* write_bio = BIO_new(BIO_s_mem());

    if (!read_bio || !write_bio)
    {
        if (read_bio)
            BIO_free(read_bio);

        if (write_bio)
            BIO_free(write_bio);

        SetOpenSslError("BIO_new failed");

        SSL_free(connection->ssl);
        SSL_CTX_free(connection->ctx);
        delete connection;

        return nullptr;
    }

    // SSL now owns both BIOs.
    SSL_set_bio(
        connection->ssl,
        read_bio,
        write_bio);

    if (SSL_set_tlsext_host_name(
            connection->ssl,
            connection->hostname.c_str()) != 1)
    {
        SetOpenSslError(
            "SSL_set_tlsext_host_name failed");

        SSL_free(connection->ssl);
        SSL_CTX_free(connection->ctx);
        delete connection;

        return nullptr;
    }

    SSL_set_connect_state(connection->ssl);

    return connection;
}

PMS_EXPORT int PMS_CALL pms_ssl_connect(
    void* connection_ptr)
{
    if (!connection_ptr)
    {
        SetLastError("Connection is null");
        return PMS_SSL_ERROR;
    }

    auto* connection =
        static_cast<PmsSslConnection*>(connection_ptr);

    int result = SSL_connect(connection->ssl);

    if (result == 1)
        return PMS_SSL_OK;

    return TranslateSslError(
        connection->ssl,
        result);
}

PMS_EXPORT int PMS_CALL pms_ssl_feed_read(
    void* connection_ptr,
    const uint8_t* buffer,
    int length)
{
    if (!connection_ptr)
    {
        SetLastError("Connection is null");
        return PMS_SSL_ERROR;
    }

    if (!buffer && length > 0)
    {
        SetLastError("Buffer is null");
        return PMS_SSL_ERROR;
    }

    if (length == 0)
        return 0;

    auto* connection =
        static_cast<PmsSslConnection*>(connection_ptr);

    BIO* bio = SSL_get_rbio(connection->ssl);

    if (!bio)
    {
        SetLastError("Read BIO is null");
        return PMS_SSL_ERROR;
    }

    int result = BIO_write(
        bio,
        buffer,
        length);

    if (result <= 0)
    {
        SetOpenSslError("BIO_write failed");
        return PMS_SSL_ERROR;
    }

    return result;
}

PMS_EXPORT int PMS_CALL pms_ssl_take_write(
    void* connection_ptr,
    uint8_t* buffer,
    int length)
{
    if (!connection_ptr)
    {
        SetLastError("Connection is null");
        return PMS_SSL_ERROR;
    }

    if (!buffer && length > 0)
    {
        SetLastError("Buffer is null");
        return PMS_SSL_ERROR;
    }

    if (length == 0)
        return 0;

    auto* connection =
        static_cast<PmsSslConnection*>(connection_ptr);

    BIO* bio = SSL_get_wbio(connection->ssl);

    if (!bio)
    {
        SetLastError("Write BIO is null");
        return PMS_SSL_ERROR;
    }

    int result = BIO_read(
        bio,
        buffer,
        length);

    if (result >= 0)
        return result;

    if (BIO_should_retry(bio))
        return 0;

    SetOpenSslError("BIO_read failed");
    return PMS_SSL_ERROR;
}

PMS_EXPORT int PMS_CALL pms_ssl_pending_write(
    void* connection_ptr)
{
    if (!connection_ptr)
    {
        SetLastError("Connection is null");
        return PMS_SSL_ERROR;
    }

    auto* connection =
        static_cast<PmsSslConnection*>(connection_ptr);

    BIO* bio = SSL_get_wbio(connection->ssl);

    if (!bio)
    {
        SetLastError("Write BIO is null");
        return PMS_SSL_ERROR;
    }

    return static_cast<int>(
        BIO_ctrl_pending(bio));
}

PMS_EXPORT int PMS_CALL pms_ssl_read(
    void* connection_ptr,
    uint8_t* buffer,
    int length)
{
    if (!connection_ptr)
    {
        SetLastError("Connection is null");
        return PMS_SSL_ERROR;
    }

    if (!buffer && length > 0)
    {
        SetLastError("Buffer is null");
        return PMS_SSL_ERROR;
    }

    auto* connection =
        static_cast<PmsSslConnection*>(connection_ptr);

    int result = SSL_read(
        connection->ssl,
        buffer,
        length);

    if (result > 0)
        return result;

    return TranslateSslError(
        connection->ssl,
        result);
}

PMS_EXPORT int PMS_CALL pms_ssl_write(
    void* connection_ptr,
    const uint8_t* buffer,
    int length)
{
    if (!connection_ptr)
    {
        SetLastError("Connection is null");
        return PMS_SSL_ERROR;
    }

    if (!buffer && length > 0)
    {
        SetLastError("Buffer is null");
        return PMS_SSL_ERROR;
    }

    auto* connection =
        static_cast<PmsSslConnection*>(connection_ptr);

    int result = SSL_write(
        connection->ssl,
        buffer,
        length);

    if (result > 0)
        return result;

    return TranslateSslError(
        connection->ssl,
        result);
}

PMS_EXPORT const char* PMS_CALL
pms_ssl_get_protocol_version(
    void* connection_ptr)
{
    if (!connection_ptr)
        return "";

    auto* connection =
        static_cast<PmsSslConnection*>(connection_ptr);

    return SSL_get_version(connection->ssl);
}

PMS_EXPORT const char* PMS_CALL
pms_ssl_get_cipher_name(
    void* connection_ptr)
{
    if (!connection_ptr)
        return "";

    auto* connection =
        static_cast<PmsSslConnection*>(connection_ptr);

    const SSL_CIPHER* cipher =
        SSL_get_current_cipher(connection->ssl);

    if (!cipher)
        return "";

    return SSL_CIPHER_get_name(cipher);
}

PMS_EXPORT const char* PMS_CALL 
pms_ssl_get_alpn_selected(
    void* connection_ptr)
{
    if (!connection_ptr)
        return "";

    auto* connection = 
        static_cast<PmsSslConnection*>(connection_ptr);

    const uint8_t* alpn_proto = nullptr;
    unsigned alpn_len = 0;

    // OpenSSL/BoringSSL native call to inspect the negotiated ALPN wire-string
    SSL_get0_alpn_selected(connection->ssl, &alpn_proto, &alpn_len);

    if (alpn_proto && alpn_len > 0)
    {
        // Thread-local error string can be repurposed or we can return a static/temporary copy.
        // For absolute safety across P/Invoke, we can return a null-terminated slice 
        // since PtrToStringAnsi handles bounded marshaling, but here we can just safely 
        // map it to g_last_error to keep it alive or create a small buffer.
        static thread_local std::string g_alpn_buffer;
        g_alpn_buffer = std::string(reinterpret_cast<const char*>(alpn_proto), alpn_len);
        return g_alpn_buffer.c_str();
    }

    return "";
}

PMS_EXPORT const char* PMS_CALL
pms_ssl_get_last_error()
{
    return g_last_error.c_str();
}

PMS_EXPORT void PMS_CALL pms_ssl_free(
    void* connection_ptr)
{
    if (!connection_ptr)
        return;

    auto* connection =
        static_cast<PmsSslConnection*>(connection_ptr);

    if (connection->ssl)
        SSL_free(connection->ssl);

    if (connection->ctx)
        SSL_CTX_free(connection->ctx);

    delete connection;
}

PMS_EXPORT int PMS_CALL pms_ssl_get_peer_certificate(
    void* connection_ptr,
    uint8_t* buffer,
    int max_length)
{
    if (!connection_ptr)
    {
        SetLastError("Connection is null");
        return PMS_SSL_ERROR;
    }

    auto* connection =
        static_cast<PmsSslConnection*>(connection_ptr);

    X509* cert = SSL_get_peer_certificate(connection->ssl);
    if (!cert)
    {
        SetLastError("No peer certificate found");
        return 0;
    }

    int cert_len = i2d_X509(cert, nullptr);
    if (cert_len <= 0)
    {
        SetOpenSslError("Failed to calculate certificate length");
        X509_free(cert);
        return PMS_SSL_ERROR;
    }

    if (!buffer)
    {
        X509_free(cert);
        return cert_len;
    }

    if (max_length < cert_len)
    {
        SetLastError("Buffer is too small for certificate");
        X509_free(cert);
        return PMS_SSL_ERROR;
    }

    uint8_t* out_ptr = buffer;
    if (i2d_X509(cert, &out_ptr) < 0)
    {
        SetOpenSslError("Failed to serialize certificate");
        X509_free(cert);
        return PMS_SSL_ERROR;
    }

    X509_free(cert);
    return cert_len;
}

}