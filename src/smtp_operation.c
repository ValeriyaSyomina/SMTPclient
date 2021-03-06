#include "smtp_operation.h"


int send_messages(struct domain_info_list* domains, mqd_t mq, int attempts_number, int attempts_delay)
{
    if (!domains)
    {
        send_log_message(mq, ERROR_LEVEL, "Null input domains");
        return NULL_DATA;
    }

    send_log_message(mq, DEBUG_LEVEL, "Start sending messages...");

    int result = 0;
    struct domain_info_list* cur = domains;
    while (cur && result >= 0)
    {        
        result = send_messages_one_domain(cur->info, mq, attempts_number, attempts_delay);
        cur = cur->next;
    }
    if (result <= 0)
        send_log_message(mq, ERROR_LEVEL, "Send messages");
    else
        send_log_message(mq, DEBUG_LEVEL, "All messages successfully send");
    return result;
}

int send_messages_one_domain(struct domain_info* domain, mqd_t mq, int attempts_number, int attempts_delay)
{
    int result = 0;
    if (!domain)
    {
        send_log_message(mq, ERROR_LEVEL, "Null input domain");
        return NULL_DATA;
    }    

    char log_message[MAX_LOGGER_MESSAGE_SIZE];
    snprintf(log_message, MAX_LOGGER_MESSAGE_SIZE, "Sending messages to domain %s", domain->domain_name);
    send_log_message(mq, DEBUG_LEVEL, log_message);

    send_log_message(mq, DEBUG_LEVEL, "Connecting to SMTP server...");
    int smtp_socket_descriptor = try_connect_to_smtp_server(domain->domain_name, attempts_number, attempts_delay);
    if (!smtp_socket_descriptor)
    {
        send_log_message(mq, ERROR_LEVEL, "Fail connect to SMTP server");
        return SOCKET_ERROR;
    }
    char buffer[MAX_RESPONCE_BUFFER_SIZE];
    bzero(buffer, MAX_RESPONCE_BUFFER_SIZE);
    read(smtp_socket_descriptor, buffer, MAX_RESPONCE_BUFFER_SIZE);
    int result_code = strtol(buffer, NULL, 10);

    send_log_message(mq, DEBUG_LEVEL, "Connection successufully established");
    send_log_message(mq, DEBUG_LEVEL, "Sending EHLO command");

    send_command_to_smtp_server(smtp_socket_descriptor, EHLO_COMMAND_NUMBER, MY_SERVER_NAME);

    bzero(buffer, MAX_RESPONCE_BUFFER_SIZE);
    read(smtp_socket_descriptor, buffer, MAX_RESPONCE_BUFFER_SIZE);
    result_code = strtol(buffer, NULL, 10);
    if (result_code != SUCCESS_OPERATION_CODE)
    {
        send_log_message(mq, ERROR_LEVEL, "Send EHLO command");
        return ERROR_RECEIVE_DATA;
    }
    send_log_message(mq, DEBUG_LEVEL, "EHLO command successufully send");

    result = send_domain_messages_list(smtp_socket_descriptor, domain->messages, mq);
    if (result <= 0)
    {
        send_log_message(mq, ERROR_LEVEL, "Send messages for domain");
        return ERROR_SEND_DATA;
    }
    send_log_message(mq, DEBUG_LEVEL, "Domain's messages' successufully send");

    send_log_message(mq, DEBUG_LEVEL, "Sending QUIT command");
    send_command_to_smtp_server(smtp_socket_descriptor, QUIT_COMMAND_NUMBER, NULL);

    bzero(buffer, MAX_RESPONCE_BUFFER_SIZE);
    read(smtp_socket_descriptor, buffer, MAX_RESPONCE_BUFFER_SIZE);
    result_code = strtol(buffer, NULL, 10);
    if (result_code != SUCCESS_QUIT_CODE)
    {
        send_log_message(mq, ERROR_LEVEL, "Send QUIT command");
        return ERROR_RECEIVE_DATA;
    }
    send_log_message(mq, DEBUG_LEVEL, "QUIT command successufully send");
    close(smtp_socket_descriptor);
    return result;
}

int send_domain_messages_list(int smtp_socket_descriptor, struct message_list* messages, mqd_t mq)
{
    int result = 0;
    if (!messages)
        return NULL_DATA;

    struct message_list* cur = messages;
    while (cur && result >= 0)
    {
        result = send_domain_message(smtp_socket_descriptor, cur->message, mq);
        cur = cur->next;
    }
    return result;
}


int send_domain_message(int smtp_socket_descriptor, struct message* message, mqd_t mq)
{
    int result = 0;

    if (!message)
        return NULL_DATA;

    send_log_message(mq, DEBUG_LEVEL, "Sending message...");

    struct message_header* from_header = get_message_header_by_key(message, HEADER_FROM);

    send_log_message(mq, DEBUG_LEVEL, "Send MAIL FROM command");
    send_command_to_smtp_server(smtp_socket_descriptor, MAIL_FROM_COMMAND_NUMBER, from_header->value);

    char buffer[MAX_RESPONCE_BUFFER_SIZE];
    bzero(buffer, MAX_RESPONCE_BUFFER_SIZE);
    read(smtp_socket_descriptor, buffer, MAX_RESPONCE_BUFFER_SIZE);
    int responce_code = strtol(buffer, NULL, 10);
    if (responce_code != SUCCESS_OPERATION_CODE)
    {
        send_log_message(mq, ERROR_LEVEL, "Send MAIL FROM command");
        return ERROR_RECEIVE_DATA;
    }

    send_log_message(mq, DEBUG_LEVEL, "MAIL FROM command successufully send");

    result = process_mail_to_headers(smtp_socket_descriptor, message->headers, mq);

    send_log_message(mq, DEBUG_LEVEL, "Send DATA command");
    send_command_to_smtp_server(smtp_socket_descriptor, DATA_COMMAND_NUMBER, NULL);

    bzero(buffer, MAX_RESPONCE_BUFFER_SIZE);
    read(smtp_socket_descriptor, buffer, MAX_RESPONCE_BUFFER_SIZE);
    responce_code = strtol(buffer, NULL, 10);
    if (responce_code != START_SEND_MSG_CODE)
    {
        send_log_message(mq, ERROR_LEVEL, "Send DATA command");
        return ERROR_RECEIVE_DATA;
    }
    send_log_message(mq, DEBUG_LEVEL, "DATA command successufully send");
    send_log_message(mq, DEBUG_LEVEL, "Send message body");

    send_command_to_smtp_server(smtp_socket_descriptor, MESSAGE_BODY_COMMAND_NUMBER, message->data);

    bzero(buffer, MAX_RESPONCE_BUFFER_SIZE);
    read(smtp_socket_descriptor, buffer, MAX_RESPONCE_BUFFER_SIZE);
    responce_code = strtol(buffer, NULL, 10);
    if (responce_code != SUCCESS_OPERATION_CODE)
    {
        send_log_message(mq, ERROR_LEVEL, "Send message body");
        return ERROR_RECEIVE_DATA;
    }
    bzero(buffer, MAX_RESPONCE_BUFFER_SIZE);
    read(smtp_socket_descriptor, buffer, MAX_RESPONCE_BUFFER_SIZE);
    responce_code = strtol(buffer, NULL, 10);
    send_log_message(mq, DEBUG_LEVEL, "Message successufully send");
    return result;
}

int process_mail_to_headers(int smtp_socket_descriptor, struct message_headers_list* headers, mqd_t mq)
{
    int result = 0;

    if (!headers)
        return NULL_DATA;
    struct message_headers_list* cur = headers;

    while (cur && result >= 0)
    {
        if (compare_message_header_key(cur->header, HEADER_TO) == 0)
            result = send_mail_to_command(smtp_socket_descriptor, cur->header, mq);
        cur = cur->next;
    }
    return result;
}

int send_mail_to_command(int smtp_socket_descriptor, struct message_header* to_header, mqd_t mq)
{
    send_log_message(mq, DEBUG_LEVEL, "Send RCPT TO command");
    int result = send_command_to_smtp_server(smtp_socket_descriptor, RCPT_TO_COMMAND_NUMBER, to_header->value);
    if (result < 0)
    {
        send_log_message(mq, ERROR_LEVEL, "Send RCPT TO command");
        return ERROR_SEND_DATA;
    }
    send_log_message(mq, DEBUG_LEVEL, "RCPT TO command successufully send");

    char buffer[MAX_RESPONCE_BUFFER_SIZE];
    bzero(buffer, MAX_RESPONCE_BUFFER_SIZE);
    read(smtp_socket_descriptor, buffer, MAX_RESPONCE_BUFFER_SIZE);
    int respoce_code = strtol(buffer, NULL, 10);
    if (respoce_code != SUCCESS_OPERATION_CODE)
    {
        return ERROR_RECEIVE_DATA;
    }
    return result;
}

int send_command_to_smtp_server(int socket_descriptor, int command_type, char* command_payload)
{
    char command[MAX_COMMAND_BUFFER_SIZE];
    bzero(command, MAX_COMMAND_BUFFER_SIZE);

    if (command_type == EHLO_COMMAND_NUMBER)
    {
        strncpy(command, EHLO_COMMAND, strlen(EHLO_COMMAND));
        strcat(command, command_payload);
    }
    else if (command_type == MAIL_FROM_COMMAND_NUMBER || command_type == RCPT_TO_COMMAND_NUMBER)
    {
        command_type == MAIL_FROM_COMMAND_NUMBER ? strncpy(command, MAIL_FROM_COMMAND, strlen(MAIL_FROM_COMMAND))
                                                 : strncpy(command, RCPT_TO_COMMAND, strlen(RCPT_TO_COMMAND));
        strcat(command, OPEN_TRIANGLE_BRACKET);
        strcat(command, command_payload);
        strcat(command, CLOSE_TRIANGLE_BRACKET);
    }
    else if (command_type == MESSAGE_BODY_COMMAND_NUMBER)
    {
        strncpy(command, command_payload, strlen(command_payload));
    }
    else if (command_type == DATA_COMMAND_NUMBER || command_type == QUIT_COMMAND_NUMBER)
    {
        command_type == DATA_COMMAND_NUMBER ? strncpy(command, DATA_COMMAND, strlen(DATA_COMMAND))
                                            : strncpy(command, QUIT_COMMAND, strlen(QUIT_COMMAND));
    }
    strcat(command, "\r\n");
    return write(socket_descriptor, command, strlen(command));
}


int try_connect_to_smtp_server(char* domain_name, int attempts_number, int attempts_delay)
{
    struct firedns_mxlist* xmlist = firedns_resolvemxlist_r(domain_name);
    int connection_stablished = 0;
    int smtp_socket = SOCKET_ERROR;
    struct firedns_mxlist* cur = xmlist;
    while (cur && !connection_stablished)
    {
        printf("Server name %s\n", cur->name);
        smtp_socket = create_socket(cur->name, SMTP_SERVER_PORT, attempts_number, attempts_delay);
        if (smtp_socket > 0)
            connection_stablished = 1;
        cur = cur->next;
    }
 //   free_xmlist(xmlist);
  //  xmlist = NULL;
    return smtp_socket;
}

void free_xmlist(struct firedns_mxlist* xmlist)
{
    struct firedns_mxlist* cur = NULL;
    for (; xmlist; xmlist = cur)
    {
        cur = xmlist->next;

        free(xmlist);
        xmlist = NULL;
    }
}

void free_firedns_ip6list(struct firedns_ip6list* list)
{
    struct firedns_ip6list *ip4_cur = list;
    while (ip4_cur)
    {
        ip4_cur = list->next;
        free(list);
        list = ip4_cur;
    }
}

void free_firedns_ip4list(struct firedns_ip4list* list)
{
    struct firedns_ip4list *ip4_cur = list;
    while (ip4_cur)
    {
        ip4_cur = list->next;
        free(list);
        list = ip4_cur;
    }
}

int create_socket(const char* host, int port, int attempts_number, int attempts_delay)
{
    struct hostent *he;
    struct in_addr **addr_list;
    int socket_found = 0;
    int result = SOCKET_ERROR;

    if ((he = gethostbyname(host )) == NULL)
        return SOCKET_ERROR;

    addr_list = (struct in_addr **) he->h_addr_list;

    for(int i = 0; addr_list[i] != NULL && !socket_found; i++)
    {
        struct sockaddr_in server;
        memset(&server, 0, sizeof(struct sockaddr_in));
        server.sin_family = AF_INET;
        server.sin_addr.s_addr = inet_addr(inet_ntoa(*addr_list[i]));
        server.sin_port = htons(SMTP_SERVER_PORT);

        int socket_descriptor = try_connect_to_socket(server, attempts_number, attempts_delay);
        if (socket_descriptor > 0)
        {
            socket_found = 1;
            result = socket_descriptor;
        }
    }
    return result;
}

int try_connect_to_socket(struct sockaddr_in server, int attempts_number, int attempts_delay)
{
    int socket_descriptor = socket(AF_INET , SOCK_STREAM , 0);
    int i = 0;
    int result = SOCKET_ERROR;
    int connection_established = 0;
    while (i < attempts_number && !connection_established)
    {
        int connect_result = connect(socket_descriptor,(struct sockaddr *)&server, sizeof(server));
        if (connect_result == 0)
        {
            connection_established = 1;
            result = socket_descriptor;
        }
        else
            sleep(attempts_delay);
        i++;
    }
    return result;
}
