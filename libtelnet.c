/*
 * The author or authors of this code dedicate any and all copyright interest
 * in this code to the public domain. We make this dedication for the benefit
 * of the public at large and to the detriment of our heirs and successors. We
 * intend this dedication to be an overt act of relinquishment in perpetuity of
 * all present and future rights to this code under copyright law. 
 */

#include "libtelnet.h"

/* initialize a telnet state tracker */
libtelnet_error_t libtelnet_init(struct libtelnet_t *telnet) {
	telnet->state = LIBTELNET_TEXT;
	telnet->buffer = 0;
	telnet->size = 0;
	telnet->length = 0;

	/* attempt to allocate default buffer */
	telnet->buffer = (unsigned char *)malloc(LIBTELNET_BUFFER_SIZE);
	if (telnet->buffer == 0)
		return LIBTELNET_ERROR_NOMEM;

	return LIBTELNET_ERROR_OK;
}

/* free up any memory allocated by a state tracker */
void libtelnet_close(struct libtelnet_t *telnet) {
	if (telnet->buffer) {
		free(telnet->buffer);
		telnet->buffer = 0;
	}
}

/* push a single byte into the state tracker */
void libtelnet_push_byte(struct libtelnet_t *telnet, unsigned char byte,
		void *user_data) {
	switch (telnet->state) {
	/* regular data */
	case LIBTELNET_STATE_TEXT:
		/* enter IAC state on IAC byte */
		if (byte == LIBTELNET_IAC)
			telnet->state = LIBTELNET_STATE_IAC;
		/* regular input byte */
		else
			libtelnet_input_cb(telnet, byte, user_data);
		break;
	case LIBTELNET_STATE_IAC:
		switch (byte) {
		/* subrequest */
		case LIBTELNET_SB:
			telnet->state = LIBTELNET_STATE_SB;
			break;
		/* negotiation commands */
		case LIBTELNET_WILL:
			telnet->state = LIBTELNET_STATE_WILL;
			break;
		case LIBTELNET_WONT:
			telnet->state = LIBTELNET_STATE_WONT;
			break;
		case LIBTELNET_DO:
			telnet->state = LIBTELNET_STATE_DO;
			break;
		case LIBTELNET_DONT:
			telnet->state = LIBTELNET_STATE_DONT;
			break;
		/* IAC escaping */
		case LIBTELNET_IAC:
			telbet->input_cb(telnet, LIBTELNET_IAC, user_data);
			libtelnet->state = LIBTELNET_STATE_TEXT;
			break;
		/* some other command */
		default:
			libtelnet_command_cb(telnet, byte, user_data);
			telnet->state = LIBTELNET_STATE_TEXT;
		}
		break;
	/* DO negotiation */
	case LIBTELNET_STATE_DO:
		libtelnet_negotiate_cb(telnet, LIBTELNET_DO, byte, user_data);
		telnet->state = LIBTELNET_STATE_TEXT;
		break;
	case LIBTELNET_STATE_DONT:
		libtelnet_negotiate_cb(telnet, LIBTELNET_DONT, byte, user_data);
		telnet->state = LIBTELNET_STATE_TEXT;
		break;
	case LIBTELNET_STATE_WILL:
		libtelnet_negotiate_cb(telnet, LIBTELNET_WILL, byte, user_data);
		telnet->state = LIBTELNET_STATE_TEXT;
		break;
	case LIBTELNET_STATE_WONT:
		libtelnet_negotiate_cb(telnet, LIBTELNET_WONT, byte, user_data);
		telnet->state = LIBTELNET_STATE_TEXT;
		break;
	/* subrequest -- buffer bytes until end request */
	case LIBTELNET_STATE_SB:
		/* IAC command in subrequest -- either IAC SE or IAC IAC */
		if (byte == LIBTELNET_IAC) {
			telnet->state = LIBTELNET_STATE_SB_IAC;
		} else {
			/* FIXME: buffer byte */
		}
		break;
	/* IAC escaping inside a subrequest */
	case LIBTELNET_STATE_SB_IAC:
		switch (byte) {
		/* end subrequest */
		case LIBTELNET_SE:
			/* zero-size buffer is a protocol error */
			if (telnet->length == 0) {
				libtelnet_error_cb(telnet, LIBTELNET_ERROR_PROTOCOL,
					user_data);
			/* process */
			} else {
				libtelnet_subrequest_cb(telnet, telnet->buffer[0],
					telnet->buffer + 1, telnet->length - 1, user_data);

				/* unallocate free buffer */
				free(telnet->buffer);
				telnet->size = 0;
				telnet->length = 0;
			}
			
			/* return to default state */
			telnet->state = LIBTELNET_STATE_TEXT;
			break;
		/* escaped IAC byte */
		case LIBTELNET_IAC:
			/* FIXME: buffer byte */
			telnet->state = LIBTELNET_STATE_SB;
			break;
		/* something else -- protocol error */
		default:
			libtelnet_error_cb(telnet, LIBTELNET_ERROR_PROTOCOL, user_data);
			telnet->state = LIBTELNET_STATE_TEXT;
			break;
		}
		break;
	}
}

/* push a byte buffer into the state tracker */
void libtelnet_push_buffer(struct libtelnet_t *telnet, unsigned char *buffer,
		size_t size, void *user_data) {
	size_t i;
	for (i = 0; i != size; ++i)
		libtelnet_push_byte(telnet, buffer[i], user_data);
}

/* send an iac command */
void libtelnet_send_command(struct libtelnet_t *telnet, unsigned char cmd,
		void *user_data) {
	unsigned char bytes[2] = { IAC, cmd };
	libtelnet_output_cb(telnet, bytes, 2, user_data);
}

/* send negotiation */
void libtelnet_send_negotiate(struct libtelnet_t *telnet, unsigned char cmd,
		unsigned char opt, void *user_data) {
	unsigned char bytes[3] = { IAC, cmd, opt };
	libtelnet_output_cb(telnet, bytes, 3, user_data);
}

/* send non-command data (escapes IAC bytes) */
void libtelnet_send_data(struct libtelnet_t *telnet, unsigned char *buffer,
		size_t size, void *user_data) {
	size_t i, l;
	for (l = i = 0; i != size; ++i) {
		/* dump prior portion of text, send escaped bytes */
		if (buffer[i] == LIBTELNET_IAC) {
			/* dump prior text if any */
			if (i != l)
				libtelnet_output_cb(telnet, buffer + l, i - l, user_data);
			l = i + 1;

			/* send escape */
			libtelnet_send_command(telnet, LIBTELNET_IAC, user_data);
		}
	}

	/* send whatever portion of buffer is left */
	if (i != l)
		libtelnet_output_cb(telnet, buffer + l, i - l, user_data);
}

/* send sub-request */
void libtelnet_send_subrequest(struct libtelnet_t *telnet, unsigned char type,
		unsigned char *buffer, size_t size, void *user_data)  {
	libtelnet_send_command(telnet, SB, user_data);
	libtelnet_send_data(telnet, &type, 1, user_data);
	libtelnet_send_data(telnet, buffer, size, user_data);
	libtelnet_send_command(telnet, SE, user_data);
}