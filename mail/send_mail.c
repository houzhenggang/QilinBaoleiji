#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <math.h>
#include <time.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

#include "send_mail.h"
#include "base64.h"

int send_mail( struct st_mail_msg_ *msg_ )
{
    struct sockaddr_in serv_addr;
    struct hostent *server;
    struct protoent *protocol;
    struct st_char_arry *char_arry_p = NULL;
    int i_addr_p = 0, sockfd;
    FILE *tmp_fp, *att_file_p;
    char bound_id_prefix[] = "------=_NextPart_", bound_id[128], buffer[READ_FILE_LEN];
    char *subject_b6 = NULL, *content_b6 = NULL;;
    time_t timet;

    protocol = getprotobyname( PROTOCOL );
    sockfd = socket( AF_INET, SOCK_STREAM, protocol->p_proto );

    if( sockfd < 0 )
        return SEND_RESULT_OPEN_SOCK_FINAL;

    if( inet_aton( msg_->server, &serv_addr.sin_addr ) != 0 )
    {
        server = gethostbyaddr(( char * )&serv_addr.sin_addr, 4, AF_INET );
    }
    else
    {
        server = gethostbyname( msg_->server );
    }

    if( server == NULL )
        return SEND_RESULT_OPEN_SOCK_FINAL;

    bzero(( char * )&serv_addr, sizeof( serv_addr ) );
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons( msg_->port );
    bcopy(( char * )server->h_addr, ( char * )&serv_addr.sin_addr.s_addr, server->h_length );

    if( connect( sockfd, ( struct sockaddr * )&serv_addr, sizeof( serv_addr ) ) < 0 )
    {
        return SEND_RESULT_CONNECT_FINAL;
    }

    if( !send_mail_header( sockfd, msg_ ) )
        return SEND_RESULT_CONNECT_FINAL;

    //return SEND_RESULT_FINAL;
    //开始处理正文
    //为了防止大附件在内存中builde时候产生内存崩溃，此处建立一个临时文件

    /* Write mail payload */
    //tmp_fp = fopen("./tmp","wb");
    tmp_fp = tmpfile();

    if( tmp_fp == NULL )
    {
        return SEND_RESULT_FINAL;
    }

    time( &timet );
    sprintf( bound_id, "abc%dxyz", time( 0 ) );
    fprintf( tmp_fp, "Message-Id: <%s@%s>\r\n", bound_id, msg_->server );
    fprintf( tmp_fp, "Date: %s", asctime( gmtime( &timet ) ) );
    fprintf( tmp_fp, "X-Priority: %d\r\n", msg_->priority );
    fprintf( tmp_fp, "From: %s\r\n", msg_->from_subject ? msg_->from_subject : msg_->from );

    for( char_arry_p = msg_->to_address_ary; char_arry_p < ( msg_->to_address_ary + msg_->to_addr_len ); char_arry_p++, i_addr_p++ )
    {
        if( i_addr_p == 0 )
            fprintf( tmp_fp, "To: " );

        fprintf( tmp_fp, "%s", char_arry_p->str_p );

        if( i_addr_p < ( msg_->to_addr_len - 1 ) )
            fprintf( tmp_fp, ";" );

        if( i_addr_p == msg_->to_addr_len - 1 )
            fprintf( tmp_fp, "\r\n" );
    }

    /* Write carbon copy */
    if( msg_->cc_address_ary )
    {
        for( i_addr_p = 0, char_arry_p = msg_->cc_address_ary; char_arry_p < ( msg_->cc_address_ary + msg_->cc_addr_len ); char_arry_p++, i_addr_p++ )
        {
            if( i_addr_p == 0 )
                fprintf( tmp_fp, "To: " );

            fprintf( tmp_fp, "%s", char_arry_p->str_p );

            if( i_addr_p < ( msg_->cc_addr_len - 1 ) )
                fprintf( tmp_fp, ";" );

            if( i_addr_p == msg_->cc_addr_len - 1 )
                fprintf( tmp_fp, "\r\n" );
        }
    }

    base64_encoder( msg_->subject, strlen( msg_->subject ), &subject_b6 );

    if( subject_b6 == NULL )
        return SEND_RESULT_FINAL;

    fprintf( tmp_fp, "Subject: =?%s?B?%s?=\r\n", msg_->charset, subject_b6 );
    fprintf( tmp_fp, "MIME-Version: 1.0\r\n" );
    fprintf( tmp_fp, "X-Mailer: \r\n" );

    //atts
    if( msg_->att_file_ary )
    {
        fprintf( tmp_fp, "Content-Type: multipart/mixed;boundary=\"%s%s\"\r\n", bound_id_prefix, bound_id );
        fprintf( tmp_fp, "\r\n\r\n" );
        fprintf( tmp_fp, "--%s%s\r\n", bound_id_prefix, bound_id );
    }

    /* Write content */
    fprintf( tmp_fp, "Content-Type: Multipart/Alternative; boundary=\"%s%s_body\"\r\n", bound_id_prefix, bound_id );
    fprintf( tmp_fp, "\r\n" );
    fprintf( tmp_fp, "--%s%s_body\r\n", bound_id_prefix, bound_id );
    fprintf( tmp_fp, "Content-Type: %s;charset=\"%s\"\r\n", ( msg_->mail_style_html == HTML_STYLE_MAIL ) ? "text/html" : "text/plain", msg_->charset );
    fprintf( tmp_fp, "Content-Transfer-Encoding: base64\r\n" );
    fprintf( tmp_fp, "\r\n" );

    base64_encoder( msg_->content, strlen( msg_->content ), &content_b6 );

    if( content_b6 == NULL )
    {
        free( subject_b6 );
        return SEND_RESULT_FINAL;
    }

    fprintf( tmp_fp, "%s", content_b6 );
    fprintf( tmp_fp, "\r\n" );
    fprintf( tmp_fp, "--%s%s_body--\r\n", bound_id_prefix, bound_id );

    free( subject_b6 );
    free( content_b6 );

    //add file.
    if( msg_->att_file_ary )
    {
        for( i_addr_p = 0, char_arry_p = msg_->att_file_ary; char_arry_p < ( msg_->att_file_ary + msg_->att_file_len ); char_arry_p++, i_addr_p++ )
        {
            att_file_p = fopen( char_arry_p->str_p, "r" );

            if( att_file_p == NULL )
                continue;

            fseek( tmp_fp, 0, SEEK_END );
            char *fn_ = char_arry_p->str_p;
            char file_name[strlen( fn_ )];

            while(( fn_ = strchr( fn_, '/' ) ) != NULL )
            {
                *fn_++;

                if( strchr( fn_, '/' ) == NULL )
                {
                    strcpy( file_name, fn_ );
                    break;
                }
            }

            //write file
            fprintf( tmp_fp, "\r\n" );
            fprintf( tmp_fp, "--%s%s\r\n", bound_id_prefix, bound_id );
            //还可以根据扩展名来判断文件的Content-Type，这里没有做，可以自己完成.
            fprintf( tmp_fp, "Content-Type: %s;name=\"%s\"\r\n", "application/octet-stream", file_name );
            fprintf( tmp_fp, "Content-Transfer-Encoding: base64\r\n" );
            fprintf( tmp_fp, "Content-Disposition: attachment; filename=\"%s\"\r\n", file_name );
            fprintf( tmp_fp, "\r\n" );
            //file 2 b6 begin...
            base64_encoder_file( att_file_p, tmp_fp );
            fclose( att_file_p );
            //file 2 b6 end...
            fprintf( tmp_fp, "\r\n" );

            if( i_addr_p == msg_->att_file_len - 1 )
                fprintf( tmp_fp, "--%s%s--\r\n", bound_id_prefix, bound_id );
        }
    }

    /* Sent mail payload */
	fseek( tmp_fp, 0, SEEK_SET );
    int frwp_tmp = fileno( tmp_fp );

    if( frwp_tmp == -1 )
    {
        return -1;
    }

    size_t byte_len = -1;

    while(( byte_len = read( frwp_tmp, buffer, sizeof( buffer ) ) ) > 0 )
    {
        write( sockfd, buffer, byte_len );
    }

    close( frwp_tmp );
    fclose( tmp_fp );
    /**********************/
    //发送文件结束
    if( !cmd_msg( sockfd, "\r\n.\r\n", "250" ) )
        return SEND_RESULT_FINAL;

    close( sockfd );
    return SEND_RESULT_SUCCESS;
}
/*********************
  发送信体头信息
 *********************/
int send_mail_header( int sockfd, struct st_mail_msg_ *msg )
{
    cmd_msg( sockfd, NULL, NULL );

    if( msg->authorization == AUTH_SEND_MAIL )
    {
        char ehlo_cmd[8+strlen( msg->server )];
        sprintf( ehlo_cmd, "EHLO %s\r\n", msg->server );

        if( !cmd_msg( sockfd, ehlo_cmd, "250 " ) )return 0;

        if( !cmd_msg( sockfd, "AUTH LOGIN\r\n", "334" ) )return 0;

        char *b6_user = NULL;
        char *b6_pswd = NULL;
        base64_encoder( msg->auth_user, strlen( msg->auth_user ), &b6_user );

        if( b6_user == NULL )
            return 0;

        base64_encoder( msg->auth_passwd, strlen( msg->auth_passwd ), &b6_pswd );

        if( b6_pswd == NULL )
        {
            if (b6_user) free( b6_user );
            return 0;
        }

        char b6_user_cmd[strlen( b6_user )+3];
        char b6_pswd_cmd[strlen( b6_pswd )+3];
        sprintf( b6_user_cmd, "%s\r\n", b6_user );
        sprintf( b6_pswd_cmd, "%s\r\n", b6_pswd );

        if( !cmd_msg( sockfd, b6_user_cmd, "334" ) )
        {
            if (b6_user) free( b6_user );
            if (b6_pswd) free( b6_pswd );
            return 0;
        }

        if( !cmd_msg( sockfd, b6_pswd_cmd, "235" ) )
        {
            if (b6_user) free( b6_user );
            if (b6_pswd) free( b6_pswd );
            return 0;
        }

        if (b6_user) free( b6_user );
        if (b6_pswd) free( b6_pswd );
        b6_user = NULL;
        b6_pswd = NULL;

    }
    else
    {
        char helo_cmd[8+strlen( msg->server )];
        sprintf( helo_cmd, "HELO %s\r\n", msg->server );

        if( !cmd_msg( sockfd, helo_cmd, "250" ) )
            return 0;
    }

    if( !msg->from )
        return 0;

    char from_cmd[13+strlen( msg->from )];
    sprintf( from_cmd, "MAIL FROM:%s\r\n", msg->from );

    if( !cmd_msg( sockfd, from_cmd, "250" ) )return 0;

    if( !msg->to_address_ary )return 0;

    struct st_char_arry *char_arry_p = NULL;

    for( char_arry_p = msg->to_address_ary; char_arry_p < ( msg->to_address_ary + msg->to_addr_len ); char_arry_p++ )
    {
        char to_addr_cmd[12+strlen( char_arry_p->str_p )];
        sprintf( to_addr_cmd, "RCPT TO:%s\r\n", char_arry_p->str_p );

        if( !cmd_msg( sockfd, to_addr_cmd, "250" ) )return 0;
    }

    for( char_arry_p = msg->bc_address_ary; char_arry_p < ( msg->bc_address_ary + msg->bc_addr_len ); char_arry_p++ )
    {
        char bc_addr_cmd[12+strlen( char_arry_p->str_p )];
        sprintf( bc_addr_cmd, "RCPT TO:%s\r\n", char_arry_p->str_p );

        if( !cmd_msg( sockfd, bc_addr_cmd, "250" ) )return 0;
    }

    for( char_arry_p = msg->cc_address_ary; char_arry_p < ( msg->cc_address_ary + msg->cc_addr_len ); char_arry_p++ )
    {
        char cc_addr_cmd[12+strlen( char_arry_p->str_p )];
        sprintf( cc_addr_cmd, "RCPT TO:%s\r\n", char_arry_p->str_p );

        if( !cmd_msg( sockfd, cc_addr_cmd, "250" ) )return 0;
    }

    if( !cmd_msg( sockfd, "DATA\r\n", "354" ) )return 0;

    return 1;
}

/******************
  sockfd : socket文件描述符
  cmd    : 要发送的字符
  flag   : 读取的字符中是否含有flag
  return 1: 成功
  0：失败
 ************************/
int cmd_msg( int sockfd, const char *cmd, const char *flag )
{
    int r_result;
    int r_len;
    int buf_len = 1024;
    int i, j;
    char buffer[buf_len];

    if( cmd && DEBUG )
    {
        j = strlen( cmd );
        printf( "Write to server %d bytes:\t", j );

        for( i = 0; i < j; i++ )
        {
            if( isprint( *( cmd + i ) ) ) printf( "%c", *( cmd + i ) );
            else printf( "%02x ", ( unsigned char )*( cmd + i ) );
        }

        printf( "\n" );
    }

    if( cmd )
    {
        r_result = write( sockfd, cmd, strlen( cmd ) );

        if( r_result < 0 )
        {
            return 0;
        }
    }

    if(( r_len = read( sockfd, buffer, buf_len ) ) < 0 )
    {
        return 0;
    }

    if( DEBUG )
    {
        j = r_len;
        printf( "Read from server %d bytes:\t", j );

        for( i = 0; i < j; i++ )
        {
            if( isprint( *( buffer + i ) ) ) printf( "%c", *( buffer + i ) );
            else printf( "%02x ", ( unsigned char )*( buffer + i ) );
        }

        printf( "\n" );
    }

    if( flag && strstr( buffer, flag ) )
    {
        return 1;
    }

    return 0;
}

void init_mail_msg( struct st_mail_msg_ *msg )
{
    msg->att_file_len = 0;
    msg->to_addr_len = 0;
    msg->bc_addr_len = 0;
    msg->cc_addr_len = 0;
    msg->authorization = 0;
    msg->priority = 3;
    msg->mail_style_html = 0;
    msg->port = 25;
    msg->charset = "gb2312";
    msg->att_file_ary = NULL;
    msg->to_address_ary = NULL;
    msg->bc_address_ary = NULL;
    msg->cc_address_ary = NULL;
    msg->subject = NULL;
    msg->content = NULL;
}

