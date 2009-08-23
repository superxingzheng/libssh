/*
 * scp - SSH scp wrapper functions
 *
 * This file is part of the SSH Library
 *
 * Copyright (c) 2009 by Aris Adamantiadis <aris@0xbadc0de.be>
 *
 * The SSH Library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or (at your
 * option) any later version.
 *
 * The SSH Library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with the SSH Library; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 59 Temple Place - Suite 330, Boston,
 * MA 02111-1307, USA.
 */

#include <stdio.h>
#include <string.h>

#include "libssh/priv.h"

/** @brief Creates a new scp session
 * @param session the SSH session to use
 * @param mode one of SSH_SCP_WRITE or SSH_SCP_READ, depending if you need to drop files remotely or read them.
 * It is not possible to combine read and write.
 * @returns NULL if the creation was impossible.
 * @returns a ssh_scp handle if it worked.
 */
ssh_scp ssh_scp_new(ssh_session session, int mode, const char *location){
  ssh_scp scp=malloc(sizeof(struct ssh_scp_struct));
  if(scp == NULL){
    ssh_set_error(session,SSH_FATAL,"Error allocating memory for ssh_scp");
    return NULL;
  }
  ZERO_STRUCTP(scp);
  if(mode != SSH_SCP_WRITE && mode != SSH_SCP_READ){
    ssh_set_error(session,SSH_FATAL,"Invalid mode %d for ssh_scp_new()",mode);
    ssh_scp_free(scp);
    return NULL;
  }
  scp->location=strdup(location);
  if (scp->location == NULL) {
    ssh_set_error(session,SSH_FATAL,"Error allocating memory for ssh_scp");
    ssh_scp_free(scp);
    return NULL;
  }
  scp->session=session;
  scp->mode=mode;
  scp->channel=NULL;
  scp->state=SSH_SCP_NEW;
  return scp;
}

int ssh_scp_init(ssh_scp scp){
  int r;
  char execbuffer[1024];
  uint8_t code;
  if(scp->state != SSH_SCP_NEW){
    ssh_set_error(scp->session,SSH_FATAL,"ssh_scp_init called under invalid state");
    return SSH_ERROR;
  }
  scp->channel=channel_new(scp->session);
  if(scp->channel == NULL){
    scp->state=SSH_SCP_ERROR;
    return SSH_ERROR;
  }
  r= channel_open_session(scp->channel);
  if(r==SSH_ERROR){
    scp->state=SSH_SCP_ERROR;
    return SSH_ERROR;
  }
  if(scp->mode == SSH_SCP_WRITE)
    snprintf(execbuffer,sizeof(execbuffer),"scp -t %s",scp->location);
  else
    snprintf(execbuffer,sizeof(execbuffer),"scp -f %s",scp->location);
  if(channel_request_exec(scp->channel,execbuffer) == SSH_ERROR){
    scp->state=SSH_SCP_ERROR;
    return SSH_ERROR;
  }
  r=channel_read(scp->channel,&code,1,0);
  if(code != 0){
    ssh_set_error(scp->session,SSH_FATAL, "scp status code %ud not valid", code);
    scp->state=SSH_SCP_ERROR;
    return SSH_ERROR;
  }
  if(scp->mode == SSH_SCP_WRITE)
    scp->state=SSH_SCP_WRITE_INITED;
  else
    scp->state=SSH_SCP_READ_INITED;
  return SSH_OK;
}

int ssh_scp_close(ssh_scp scp){
  if(scp->channel != NULL){
    if(channel_send_eof(scp->channel) == SSH_ERROR){
      scp->state=SSH_SCP_ERROR;
      return SSH_ERROR;
    }
    if(channel_close(scp->channel) == SSH_ERROR){
      scp->state=SSH_SCP_ERROR;
      return SSH_ERROR;
    }
    channel_free(scp->channel);
    scp->channel=NULL;
  }
  scp->state=SSH_SCP_NEW;
  return SSH_OK;
}

void ssh_scp_free(ssh_scp scp){
  if(scp->state != SSH_SCP_NEW)
    ssh_scp_close(scp);
  if(scp->channel)
    channel_free(scp->channel);
  SAFE_FREE(scp->location);
  SAFE_FREE(scp->request_mode);
  SAFE_FREE(scp->request_name);
  SAFE_FREE(scp);
}

/** @brief creates a directory in a scp in sink mode
 * @param dirname Name of the directory being created. 
 * @param perms Text form of the unix permissions for the new directory, e.g. "0755".
 * @returns SSH_OK if the directory was created.
 * @returns SSH_ERROR if an error happened.
 * @see ssh_scp_leave_directory
 */
int ssh_scp_push_directory(ssh_scp scp, const char *dirname, const char *perms){
  char buffer[1024];
  int r;
  uint8_t code;
  char *dir;
  if(scp->state != SSH_SCP_WRITE_INITED){
    ssh_set_error(scp->session,SSH_FATAL,"ssh_scp_push_directory called under invalid state");
    return SSH_ERROR;
  }
  dir=ssh_basename(dirname);
  snprintf(buffer, sizeof(buffer), "D%s 0 %s\n", perms, dir);
  SAFE_FREE(dir);
  r=channel_write(scp->channel,buffer,strlen(buffer));
  if(r==SSH_ERROR){
    scp->state=SSH_SCP_ERROR;
    return SSH_ERROR;
  }
  r=channel_read(scp->channel,&code,1,0);
  if(code != 0){
    ssh_set_error(scp->session,SSH_FATAL, "scp status code %ud not valid", code);
    scp->state=SSH_SCP_ERROR;
    return SSH_ERROR;
  }
  return SSH_OK;
}

/** 
 * @brief Leaves a directory
 * @returns SSH_OK if the directory was created.
 * @returns SSH_ERROR if an error happened.
 * @see ssh_scp_push_directory
 */
 int ssh_scp_leave_directory(ssh_scp scp){
  char buffer[1024];
  int r;
  uint8_t code;
  if(scp->state != SSH_SCP_WRITE_INITED){
    ssh_set_error(scp->session,SSH_FATAL,"ssh_scp_leave_directory called under invalid state");
    return SSH_ERROR;
  }
  strcpy(buffer, "E\n");
  r=channel_write(scp->channel,buffer,strlen(buffer));
  if(r==SSH_ERROR){
    scp->state=SSH_SCP_ERROR;
    return SSH_ERROR;
  }
  r=channel_read(scp->channel,&code,1,0);
  if(code != 0){
    ssh_set_error(scp->session,SSH_FATAL, "scp status code %ud not valid", code);
    scp->state=SSH_SCP_ERROR;
    return SSH_ERROR;
  }
  return SSH_OK;
}


/** @brief initializes the sending of a file to a scp in sink mode
 * @param filename Name of the file being sent. It should not contain any path indicator
 * @param size Exact size in bytes of the file being sent.
 * @param perms Text form of the unix permissions for the new file, e.g. "0644"
 * @returns SSH_OK if the file is ready to be sent.
 * @returns SSH_ERROR if an error happened.
 */
int ssh_scp_push_file(ssh_scp scp, const char *filename, size_t size, const char *perms){
  char buffer[1024];
  int r;
  uint8_t code;
  char *file;
  if(scp->state != SSH_SCP_WRITE_INITED){
    ssh_set_error(scp->session,SSH_FATAL,"ssh_scp_push_file called under invalid state");
    return SSH_ERROR;
  }
  file=ssh_basename(filename);
  snprintf(buffer, sizeof(buffer), "C%s %" PRIdS " %s\n", perms, size, file);
  SAFE_FREE(file);
  r=channel_write(scp->channel,buffer,strlen(buffer));
  if(r==SSH_ERROR){
    scp->state=SSH_SCP_ERROR;
    return SSH_ERROR;
  }
  r=channel_read(scp->channel,&code,1,0);
  if(code != 0){
    ssh_set_error(scp->session,SSH_FATAL, "scp status code %ud not valid", code);
    scp->state=SSH_SCP_ERROR;
    return SSH_ERROR;
  }
  scp->filelen = size;
  scp->processed = 0;
  scp->state=SSH_SCP_WRITE_WRITING;
  return SSH_OK;
}

/** @brief Write into a remote scp file
 * @param buffer the buffer to write
 * @param len the number of bytes to write
 * @returns SSH_OK the write was successful
 * @returns SSH_ERROR an error happened while writing
 */
int ssh_scp_write(ssh_scp scp, const void *buffer, size_t len){
  int w;
  //int r;
  //uint8_t code;
  if(scp->state != SSH_SCP_WRITE_WRITING){
    ssh_set_error(scp->session,SSH_FATAL,"ssh_scp_write called under invalid state");
    return SSH_ERROR;
  }
  if(scp->processed + len > scp->filelen)
    len = scp->filelen - scp->processed;
  /* hack to avoid waiting for window change */
  channel_poll(scp->channel,0);
  w=channel_write(scp->channel,buffer,len);
  if(w != SSH_ERROR)
    scp->processed += w;
  else {
    scp->state=SSH_SCP_ERROR;
    //return=channel_get_exit_status(scp->channel);
    return SSH_ERROR;
  }
  /* Check if we arrived at end of file */
  if(scp->processed == scp->filelen) {
/*    r=channel_read(scp->channel,&code,1,0);
    if(r==SSH_ERROR){
      scp->state=SSH_SCP_ERROR;
      return SSH_ERROR;
    }
    if(code != 0){
      ssh_set_error(scp->session,SSH_FATAL, "scp status code %ud not valid", code);
      scp->state=SSH_SCP_ERROR;
      return SSH_ERROR;
    }
*/
    scp->processed=scp->filelen=0;
    scp->state=SSH_SCP_WRITE_INITED;
  }
  return SSH_OK;
}

/**
 * @brief reads a string on a channel, terminated by '\n'
 * @param buffer pointer to a buffer to place the string
 * @param len size of the buffer in bytes. If the string is bigger
 * than len-1, only len-1 bytes are read and the string
 * is null-terminated.
 * @returns SSH_OK The string was read
 * @returns SSH_ERROR Error happened while reading
 */
int ssh_scp_read_string(ssh_scp scp, char *buffer, size_t len){
  size_t r=0;
  int err=SSH_OK;
  while(r<len-1){
    err=channel_read(scp->channel,&buffer[r],1,0);
    if(err==SSH_ERROR){
      break;
    }
    if(err==0){
      ssh_set_error(scp->session,SSH_FATAL,"End of file while reading string");
      err=SSH_ERROR;
      break;
    }
    r++;
    if(buffer[r-1] == '\n')
      break;
  }
  buffer[r]=0;
  return err;
}

/** @brief waits for a scp request (file, directory)
 * @returns SSH_ERROR Some error happened
 * @returns SSH_SCP_REQUEST_FILE The other side is sending a file
 * @returns SSH_SCP_REQUEST_DIRECTORY The other side is sending a directory
 * @returns SSH_SCP_REQUEST_END_DIRECTORY The other side has finished with the current directory
 * @see ssh_scp_read
 * @see ssh_scp_deny_request
 * @see ssh_scp_accept_request
 */
int ssh_scp_pull_request(ssh_scp scp){
  char buffer[4096];
  char *mode=NULL;
  char *p,*tmp;
  size_t size;
  char *name=NULL;
  int err;
  if(scp->state != SSH_SCP_READ_INITED){
    ssh_set_error(scp->session,SSH_FATAL,"ssh_scp_pull_request called under invalid state");
    return SSH_ERROR;
  }
  err=ssh_scp_read_string(scp,buffer,sizeof(buffer));
  if(err==SSH_ERROR)
    return err;
  switch(buffer[0]){
    case 'C':
      /* File */
    case 'D':
      /* Directory */
      p=strchr(buffer,' ');
      if(p==NULL)
        goto error;
      *p='\0';
      p++;
      mode=strdup(&buffer[1]);
      tmp=p;
      p=strchr(p,' ');
      if(p==NULL)
        goto error;
      *p=0;
      size=strtoull(tmp,NULL,10);
      p++;
      tmp=p;
      p=strchr(p,'\n');
      if(p==NULL)
        goto error;
      *p=0;
      name=strdup(tmp);
      SAFE_FREE(scp->request_name);
      SAFE_FREE(scp->request_mode);
      scp->request_mode=mode;
      scp->request_name=name;
      if(buffer[0]=='C'){
        scp->filelen=size;
        scp->request_type=SSH_SCP_REQUEST_NEWFILE;
      } else {
        scp->filelen='0';
        scp->request_type=SSH_SCP_REQUEST_NEWDIR;
      }
      scp->state=SSH_SCP_READ_REQUESTED;
      scp->processed = 0;
      return scp->request_type;
      break;
    case 'T':
      /* Timestamp */
    default:
      ssh_set_error(scp->session,SSH_FATAL,"Unhandled message: %s",buffer);
      return SSH_ERROR;
  }

  /* a parsing error occured */
  error:
  SAFE_FREE(name);
  SAFE_FREE(mode);
  ssh_set_error(scp->session,SSH_FATAL,"Parsing error while parsing message: %s",buffer);
  return SSH_ERROR;
}

/**
 * @brief denies the transfer of a file or creation of a directory
 *  coming from the remote party
 *  @param reason nul-terminated string with a human-readable explanation
 *  of the deny
 *  @returns SSH_OK the message was sent
 *  @returns SSH_ERROR Error sending the message, or sending it in a bad state
 */
int ssh_scp_deny_request(ssh_scp scp, const char *reason){
  char buffer[4096];
  int err;
  if(scp->state != SSH_SCP_READ_REQUESTED){
    ssh_set_error(scp->session,SSH_FATAL,"ssh_scp_deny_request called under invalid state");
    return SSH_ERROR;
  }
  snprintf(buffer,sizeof(buffer),"%c%s\n",2,reason);
  err=channel_write(scp->channel,buffer,strlen(buffer));
  if(err==SSH_ERROR) {
    return SSH_ERROR;
  }
  else {
    scp->state=SSH_SCP_READ_INITED;
    return SSH_OK;
  }
}

/**
 * @brief accepts transfer of a file or creation of a directory
 *  coming from the remote party
 *  @returns SSH_OK the message was sent
 *  @returns SSH_ERROR Error sending the message, or sending it in a bad state
 */
int ssh_scp_accept_request(ssh_scp scp){
  char buffer[]={0x00};
  int err;
  if(scp->state != SSH_SCP_READ_REQUESTED){
    ssh_set_error(scp->session,SSH_FATAL,"ssh_scp_deny_request called under invalid state");
    return SSH_ERROR;
  }
  err=channel_write(scp->channel,buffer,1);
  if(err==SSH_ERROR) {
    return SSH_ERROR;
  }
  if(scp->request_type==SSH_SCP_REQUEST_NEWFILE)
    scp->state=SSH_SCP_READ_READING;
  else
    scp->state=SSH_SCP_READ_INITED;
  return SSH_OK;
}

/** @brief Read from a remote scp file
 * @param buffer Destination buffer
 * @param size Size of the buffer
 * @returns Number of bytes read
 * @returns SSH_ERROR An error happened while reading
 */
int ssh_scp_read(ssh_scp scp, void *buffer, size_t size){
  int r;
  if(scp->state == SSH_SCP_READ_REQUESTED && scp->request_type == SSH_SCP_REQUEST_NEWFILE){
    r=ssh_scp_accept_request(scp);
    if(r==SSH_ERROR)
      return r;
  }
  if(scp->state != SSH_SCP_READ_READING){
    ssh_set_error(scp->session,SSH_FATAL,"ssh_scp_read called under invalid state");
    return SSH_ERROR;
  }
  if(scp->processed + size > scp->filelen)
    size = scp->filelen - scp->processed;
  if(size > 65536)
    size=65536; /* avoid too large reads */
  r=channel_read(scp->channel,buffer,size,0);
  if(r != SSH_ERROR)
    scp->processed += r;
  else {
    scp->state=SSH_SCP_ERROR;
    return SSH_ERROR;
  }
  /* Check if we arrived at end of file */
  if(scp->processed == scp->filelen) {
    scp->processed=scp->filelen=0;
    scp->state=SSH_SCP_READ_INITED;
  }
  return r;
}

/** Gets the name of the directory or file being
 * pushed from the other party
 * @returns file name. Should not be freed.
 */
const char *ssh_scp_request_get_filename(ssh_scp scp){
  return scp->request_name;
}

/** Gets the permissions of the directory or file being
 * pushed from the other party
 * @returns Unix permission string, e.g "0644". Should not be freed.
 */
const char *ssh_scp_request_get_permissions(ssh_scp scp){
  return scp->request_mode;
}

/** Gets the size of the file being pushed
 * from the other party
 * @returns Numeric size of the file being read.
 */
size_t ssh_scp_request_get_size(ssh_scp scp){
  return scp->filelen;
}