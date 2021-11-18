#include "receptor.h"

int r = 1;

int receiveSet(int fd) {
  State state = START;
  u_int8_t mem[3];

  while (state != STOP) {
    if (receiveSupervisionFrame(&state, fd, EMISSOR_CMD_ABYTE, SET_CONTROL_BYTE, NULL, mem) < 0)
      return 1;
  }

  printf("Sending UA...\n");
  if (sendSupervisionFrame(fd, RECEPTOR_ANSWER_ABYTE, UA_CONTROL_BYTE)) return 1;

  return 0;
}

int receiveDisc(int fd) {
  State state = START;
  u_int8_t mem[3];
  
  while (state != STOP) {
    if (receiveSupervisionFrame(&state, fd, EMISSOR_CMD_ABYTE, DISC_CONTROL_BYTE, NULL, mem) < 0)
      return 1;
  }
  printf("Received DISC\n");

  if (sendSupervisionFrame(fd, RECEPTOR_ANSWER_ABYTE, DISC_CONTROL_BYTE)) return 1;
  printf("Sent DISC\n");

  while (state != STOP) {
    if (receiveSupervisionFrame(&state, fd, EMISSOR_CMD_ABYTE, UA_CONTROL_BYTE, NULL, mem) < 0)
      return 1;
  }
  printf("Received UA\n");

  return 0;
}

int destuffData(u_int8_t* stuffed_data, int size, u_int8_t* buffer, u_int8_t* bcc2) {
  u_int8_t destuffed_buffer[FRAME_DATA_SIZE + 1];
  int bufferIdx = 0;

  for (int i = 0; i < size; i++) {
    u_int8_t currByte = stuffed_data[i];
    if (currByte == ESC_BYTE) {
      u_int8_t nextByte = stuffed_data[++i];
      if (nextByte == STUFFED_FLAG_BYTE) destuffed_buffer[bufferIdx++] = FLAG_BYTE;
      else if (nextByte == STUFFED_ESC_BYTE) destuffed_buffer[bufferIdx++] = ESC_BYTE;
      else printf("There should be no isolated ESC byte \n");
    } else destuffed_buffer[bufferIdx++] = currByte;
  }
  bufferIdx--;

  *bcc2 = destuffed_buffer[bufferIdx];

  memcpy(buffer, destuffed_buffer, bufferIdx);
  return bufferIdx;
}

int receiveDataFrame(int fd, u_int8_t* data) {
  State state = START;
  u_int8_t receivedAddress, receivedControl, calculatedBCC,
          ctrl = INFO_CONTROL_BYTE(1-r), repeatedCtrl = INFO_CONTROL_BYTE(r), calculatedBCC2, bcc2;

  int currentDataIdx, isRepeated;
  u_int8_t stuffed_data[MAX_STUFFED_DATA_SIZE];

  while (state != STOP) {
    int res; u_int8_t byte;
    res = read(fd, &byte, 1);
    if (res == -1) {
      printf("Read error\n");
      return 1;
    }

    switch (state) {
      case START:
        if (byte == FLAG_BYTE) state = FLAG_RCV;
        break;

      case FLAG_RCV:
        isRepeated = 0;
        if (byte == FLAG_BYTE) continue;
        else if (byte == EMISSOR_CMD_ABYTE) {
          receivedAddress = byte;
          state = ADDR_RCV;
        }
        else state = START;
        break;

      case ADDR_RCV:
        if (byte == repeatedCtrl) isRepeated = 1;

        if (byte == FLAG_BYTE) state = FLAG_RCV;
        else if (byte == ctrl || isRepeated) {
          receivedControl = byte;
          calculatedBCC = receivedAddress ^ receivedControl;
          state = CTRL_RCV;
        }
        else state = START;
        break;

      case CTRL_RCV:
        if (byte == FLAG_BYTE) state = FLAG_RCV;
        else if (calculatedBCC == byte) {
          state = BCC_OK;
          currentDataIdx = 0;
        }
        else state = START;
        break;

      case BCC_OK:
        if (currentDataIdx >= MAX_STUFFED_DATA_SIZE) state = START;
        else if (byte == FLAG_BYTE) {
          int dataSize = destuffData(stuffed_data, currentDataIdx, data, &bcc2);
          calculatedBCC2 = generateBCC2(data, dataSize);

          if (isRepeated) {
            if (sendSupervisionFrame(fd, RECEPTOR_ANSWER_ABYTE, RR_CONTROL_BYTE(r))) return 1;
            state = START;
          }
          else if (calculatedBCC2 != bcc2) {
            if (sendSupervisionFrame(fd, RECEPTOR_ANSWER_ABYTE, REJ_CONTROL_BYTE(1 - r))) return 1;
            state = START;
          }
          else {
            state = STOP;
          }
        }
        else stuffed_data[currentDataIdx++] = byte;
        break;
    }
  }

  printf("Sending RR...\n");
  if (sendSupervisionFrame(fd, RECEPTOR_ANSWER_ABYTE, RR_CONTROL_BYTE(1 - r))) return 1;
  r = 1 - r;
  return 0;
}


int main(int argc, char** argv) {
    int fd, c, res_read, i = 0;
    struct termios oldtio, newtio;
    u_int8_t buf[255];

    if ( (argc < 2) || 
  	     ((strcmp("/dev/ttyS0", argv[1])!=0) && 
  	      (strcmp("/dev/ttyS1", argv[1])!=0) )) {
      printf("Usage:\tnserial SerialPort\n\tex: nserial /dev/ttyS1\n");
      exit(1);
    }

    /*
    Open serial port device for reading and writing and not as controlling tty
    because we don't want to get killed if linenoise sends CTRL-C.
    */
    
    fd = open(argv[1], O_RDWR | O_NOCTTY);
    if (fd < 0) {
        perror(argv[1]); exit(-1);
    }

    if (tcgetattr(fd,&oldtio) == -1) { /* save current port settings */
      perror("tcgetattr");
      exit(-1);
    }

    bzero(&newtio, sizeof(newtio));
    newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;

    /* set input mode (non-canonical, no echo,...) */
    newtio.c_lflag = 0;
    newtio.c_cc[VTIME]    = 0;   /* inter-character timer unused */
    newtio.c_cc[VMIN]     = 1;   /* reading 1 char at a time */


  /* 
    VTIME e VMIN devem ser alterados de forma a proteger com um temporizador a 
    leitura do(s) pr�ximo(s) caracter(es)
  */

    tcflush(fd, TCIOFLUSH);

    if (tcsetattr(fd,TCSANOW,&newtio) == -1) {
      perror("tcsetattr");
      exit(-1);
    }

    printf("New termios structure set\n");
    printf("Reading the SET message...\n");

    if (receiveSet(fd)) exit(1);
    
    printf("Received Set\n");

    u_int8_t dataBuffer[FRAME_DATA_SIZE];
    if (receiveDataFrame(fd, dataBuffer)) exit(1);
    printf("Received data: %s\n", dataBuffer);

    if (receiveDisc(fd)) exit(1);
    printf("Disconnecting...\n");

    sleep(1); // Avoid changing config before sending data (transmission error)

    tcsetattr(fd,TCSANOW,&oldtio);
    close(fd);
    printf("Done\n");
    return 0;
}
