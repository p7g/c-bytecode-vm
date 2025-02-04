// #include <sys/types.h>
#include <arpa/inet.h>
#include <limits.h>
#include <netdb.h>
#include <stdlib.h>
#include <sys/socket.h>

#include "builtin_modules.h"
#include "bytes.h"
#include "error.h"
#include "gc.h"
#include "intrinsics.h"
#include "module.h"
#include "str.h"
#include "value.h"

#define CONVERT_TO_C_INT(OUT, VAL) ({ \
		if (expect_c_int(#OUT, (VAL), &OUT)) \
			return 1; \
	})
#define STRUCT_ADDR_HOST(S) ((S)->fields[0])
#define STRUCT_ADDR_PORT(S) ((S)->fields[1])

static struct cb_struct_spec *get_address_struct_spec(void)
{
	static struct cb_struct_spec *spec = NULL;

	if (spec)
		return spec;

	spec = cb_declare_struct("address", 2, "host", "port");
	return spec;
}

static int expect_c_int(const char *name, struct cb_value val, int *out)
{
	CB_EXPECT_TYPE(CB_VALUE_INT, val);

	int64_t intval = val.val.as_int;

	if (intval > INT_MAX || intval < INT_MIN) {
		cb_error_set(cb_value_from_fmt("%s is out of bounds", name));
		return 1;
	}

	*out = intval;
	return 0;
}

#define CB_HTONS(OUT, VAL) ({ \
		unsigned short _result; \
		if (expect_uint16(#OUT, (VAL), &_result)) \
			return 1; \
		(OUT) = htons(_result); \
	})

static int expect_uint16(const char *name, struct cb_value val, uint16_t *out)
{
	CB_EXPECT_TYPE(CB_VALUE_INT, val);

	int64_t intval = val.val.as_int;

	if (intval > UINT16_MAX || intval < 0) {
		cb_error_set(cb_value_from_fmt("%s is out of bounds", name));
		return 1;
	}

	*out = intval;
	return 0;
}

static struct cb_struct_spec *get_addrinfo_struct_spec(void)
{
	static struct cb_struct_spec *spec = NULL;

	if (spec)
		return spec;

	spec = cb_declare_struct("addrinfo", 4, "family", "socktype",
			"protocol", "addr");
	return spec;
}

static int sockaddr_val(struct sockaddr *sa, struct cb_value *result)
{
	void *addr;
	short port;

	if (sa->sa_family == AF_INET) {
		struct sockaddr_in *ipv4 = (struct sockaddr_in *) sa;
		addr = &ipv4->sin_addr;
		port = ipv4->sin_port;
	} else {
		struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *) sa;
		addr = &ipv6->sin6_addr;
		port = ipv6->sin6_port;
	}

	char ipstr[INET6_ADDRSTRLEN];
	if (!inet_ntop(sa->sa_family, addr, ipstr, INET6_ADDRSTRLEN)) {
		cb_error_from_errno();
		return 1;
	}

	result->type = CB_VALUE_STRUCT;
	result->val.as_struct = cb_struct_make(get_address_struct_spec(),
			cb_value_from_string(ipstr), cb_int(ntohs(port)));
	return 0;
}

static struct cb_struct *addrinfo_to_struct(struct addrinfo *info)
{
	struct cb_value addr_val;
	if (sockaddr_val(info->ai_addr, &addr_val))
		return NULL;

	return cb_struct_make(get_addrinfo_struct_spec(),
			cb_int(info->ai_family),
			cb_int(info->ai_socktype),
			cb_int(info->ai_protocol),
			addr_val);
}

static struct cb_array *addrinfo_to_array(struct addrinfo *info)
{
	unsigned len = 0;

	struct addrinfo *tmp = info;
	while (tmp) {
		len += 1;
		tmp = tmp->ai_next;
	}

	struct cb_array *arr = cb_array_new(len);
	cb_gc_hold_key *hold = cb_array_gc_hold(arr);

	unsigned i = 0;
	while (info) {
		struct cb_struct *ai_struct = addrinfo_to_struct(info);
		if (!ai_struct)
			goto err;

		arr->values[i].type = CB_VALUE_STRUCT;
		arr->values[i++].val.as_struct = ai_struct;
		info = info->ai_next;
	}

	cb_gc_release(hold);
	return arr;

err:
	cb_gc_release(hold);
	return NULL;
}

static int getaddrinfo_impl(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	struct cb_value node_val, service_val;

	node_val = argv[0];
	service_val = argv[1];

	cb_str node_str;
	const char *node;
	if (node_val.type == CB_VALUE_NULL) {
		node = NULL;
	} else {
		node_str = CB_EXPECT_STRING(node_val);
		node = cb_strptr(&node_str);
	}

	cb_str service_str;
	const char *service;
	if (service_val.type == CB_VALUE_NULL) {
		service = NULL;
	} else {
		service_str = CB_EXPECT_STRING(service_val);
		service = cb_strptr(&service_str);
	}

	int flags = 0;
	if (argc > 2)
		CONVERT_TO_C_INT(flags, argv[2]);

	struct addrinfo *server;
	struct addrinfo hints = {0};
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = flags;

	int status = getaddrinfo(node, service, &hints, &server);
	if (status) {
		cb_error_set(cb_value_from_string(gai_strerror(status)));
		return 1;
	}

	result->type = CB_VALUE_ARRAY;
	result->val.as_array = addrinfo_to_array(server);
	freeaddrinfo(server);
	return result->val.as_array == NULL;
}

static int socket_impl(size_t argc, struct cb_value *argv, struct cb_value *result)
{
	int domain, type, protocol;

	CONVERT_TO_C_INT(domain, argv[0]);
	CONVERT_TO_C_INT(type, argv[1]);
	CONVERT_TO_C_INT(protocol, argv[2]);

	int fd = socket(domain, type, protocol);
	if (fd == -1) {
		cb_error_from_errno();
		return 1;
	}

	result->type = CB_VALUE_INT;
	result->val.as_int = fd;
	return 0;
}

static int inet_pton_wrapped(int family, const char *addrstr, void *addr)
{
	int status;
	if ((status = inet_pton(family, addrstr, addr)) != 1) {
		if (status == 0) {
			cb_error_set(cb_value_from_string("Failed to parse address"));
		} else {
			cb_error_from_errno();
		}
		return 1;
	}
	return 0;
}

static int parse_address(struct cb_value address_val,
		struct sockaddr_storage *addr, int *addr_size)
{
	short port;
	char *host;
	struct cb_struct *address_struct;
	struct cb_value host_val, port_val;

	CB_EXPECT_STRUCT(get_address_struct_spec(), address_val);
	address_struct = address_val.val.as_struct;
	host_val = STRUCT_ADDR_HOST(address_struct);
	port_val = STRUCT_ADDR_PORT(address_struct);

	cb_str host_str = CB_EXPECT_STRING(host_val);
	CB_HTONS(port, port_val);
	host = cb_strptr(&host_str);

	int is_ipv6 = !!strchr(host, ':');

	if (is_ipv6) {
		if (inet_pton_wrapped(AF_INET6, host, addr))
			return 1;
		((struct sockaddr_in6 *) addr)->sin6_port = port;
		((struct sockaddr_in6 *) addr)->sin6_family = AF_INET6;
		*addr_size = sizeof(struct sockaddr_in6);
	} else {
		if (inet_pton_wrapped(AF_INET, host, addr))
			return 1;
		((struct sockaddr_in *) addr)->sin_port = port;
		((struct sockaddr_in *) addr)->sin_family = AF_INET;
		*addr_size = sizeof(struct sockaddr_in);
	}

	return 0;
}

static int connect_impl(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	int fd;
	struct sockaddr_storage addr;
	int addrlen = sizeof addr;

	CONVERT_TO_C_INT(fd, argv[0]);
	if (parse_address(argv[1], &addr, &addrlen))
		return 1;

	if (connect(fd, (struct sockaddr *) &addr, addrlen) == -1) {
		cb_error_from_errno();
		return 1;
	}

	result->type = CB_VALUE_NULL;
	return 0;
}

static int send_impl(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	int fd, sent;
	struct cb_bytes *buf;
	ssize_t buf_len;

	CONVERT_TO_C_INT(fd, argv[0]);
	CB_EXPECT_TYPE(CB_VALUE_BYTES, argv[1]);
	buf = argv[1].val.as_bytes;
	buf_len = cb_bytes_len(buf);

	sent = 0;
	while (sent < buf_len) {
		int sent2 = send(fd, cb_bytes_ptr(buf) + sent, buf_len - sent,
				MSG_NOSIGNAL);
		if (sent2 == -1) {
			cb_error_from_errno();
			return 1;
		}
		sent += sent2;
	}

	result->type = CB_VALUE_NULL;
	return 0;
}

static int recv_impl(size_t argc, struct cb_value *argv,
		struct cb_value *result)
{
	int fd, amount, received;
	char *data;
	struct cb_bytes *bytes;

	CONVERT_TO_C_INT(fd, argv[0]);
	CONVERT_TO_C_INT(amount, argv[1]);

	data = malloc(amount * sizeof(char));
	received = recv(fd, data, amount, 0);
	bytes = cb_bytes_new(received);
	memcpy(cb_bytes_ptr(bytes), data, received);
	free(data);

	result->type = CB_VALUE_BYTES;
	result->val.as_bytes = bytes;
	return 0;
}

#define CONSTS(X) \
	X(AF_INET) \
	X(AF_INET6) \
	X(SOCK_STREAM) \
	X(SOCK_DGRAM) \
	X(IPPROTO_TCP) \
	X(IPPROTO_UDP) \
	X(AI_PASSIVE)

#define DECL_CONST_IDENT(NAME) size_t ident_ ## NAME;
CONSTS(DECL_CONST_IDENT)
#undef DECL_CONST_IDENT

size_t ident_addrinfo, ident_address, ident_connect, ident_getaddrinfo,
       ident_recv, ident_send, ident_socket;

void cb_socket_build_spec(cb_modspec *spec)
{
	CB_DEFINE_EXPORT(spec, "address", ident_address);
	CB_DEFINE_EXPORT(spec, "addrinfo", ident_addrinfo);
	CB_DEFINE_EXPORT(spec, "getaddrinfo", ident_getaddrinfo);
	CB_DEFINE_EXPORT(spec, "socket", ident_socket);
	CB_DEFINE_EXPORT(spec, "connect", ident_connect);
	CB_DEFINE_EXPORT(spec, "send", ident_send);
	CB_DEFINE_EXPORT(spec, "recv", ident_recv);

#define DEFINE_CONST(NAME) CB_DEFINE_EXPORT(spec, #NAME, ident_ ## NAME);
	CONSTS(DEFINE_CONST)
#undef DEFINE_CONST
}

void cb_socket_instantiate(struct cb_module *mod)
{
	struct cb_value addrinfo_spec, address_spec;
	addrinfo_spec.type = CB_VALUE_STRUCT_SPEC;
	addrinfo_spec.val.as_struct_spec = get_addrinfo_struct_spec();
	address_spec.type = CB_VALUE_STRUCT_SPEC;
	address_spec.val.as_struct_spec = get_address_struct_spec();

	CB_SET_EXPORT(mod, ident_address, address_spec);
	CB_SET_EXPORT(mod, ident_addrinfo, addrinfo_spec);
	CB_SET_EXPORT_FN(mod, ident_getaddrinfo, 2, getaddrinfo_impl);
	CB_SET_EXPORT_FN(mod, ident_socket, 3, socket_impl);
	CB_SET_EXPORT_FN(mod, ident_connect, 3, connect_impl);
	CB_SET_EXPORT_FN(mod, ident_send, 2, send_impl);
	CB_SET_EXPORT_FN(mod, ident_recv, 2, recv_impl);

#define EXPORT_CONST(NAME) CB_SET_EXPORT(mod, ident_ ## NAME, cb_int(NAME));
	CONSTS(EXPORT_CONST)
#undef EXPORT_CONST
}