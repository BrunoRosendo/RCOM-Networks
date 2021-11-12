#include "common.h"
#include "signal.h"
#include "emissor.h"
#include "statemachines.h"

#pragma GCC diagnostic ignored "-Wimplicit-function-declaration"

const u_int8_t BCC_SET = EMISSOR_CMD_ABYTE ^ SET_CONTROL_BYTE; // Protection fields
const u_int8_t BCC_UA = RECEPTOR_ANSWER_ABYTE ^ UA_CONTROL_BYTE;

volatile int STOP_EXEC=FALSE;

int messageFlag = 1, conta = 0, s = 0;

void resetAlarmVariables() {
  messageFlag = 1, conta = 0;
}

void atende() {
   printf("alarme # %d\n", conta + 1);
   messageFlag = 1;
   conta++;
}

int sendSet(int fd) {
  resetAlarmVariables();

  State state = START;
  u_int8_t mem[3], ans[5] = {FLAG_BYTE, EMISSOR_CMD_ABYTE, SET_CONTROL_BYTE, BCC_SET, FLAG_BYTE}; 

  while (state != STOP) {
    if (conta == 3) {
      printf("Communication failed\n");
      return 1;
    }

    if (messageFlag) {
      write(fd, ans, sizeof(ans));
      messageFlag = 0;
      state = START;
      alarm(ALARM_INTERVAL);
    }

    if (receive_supervision_machine(&state, fd, RECEPTOR_ANSWER_ABYTE, UA_CONTROL_BYTE, mem))
      return 1;
  }

  printf("Read UA, success!\n");
  return 0;
}

/**
 * @brief Create control byte for information frames
 * 
 * @param s Either 0 or 1
 * @return u_int8_t control byte
 */
u_int8_t generateInfoControlByte(int s) {
  if (s == 0) return 0;

  return BIT(6);
}

/**
 * @brief Calculate BCC2 according to data by doing XOR iteratively
 * 
 * @param data 
 * @param dataSize 
 * @return u_int8_t BCC2
 */
u_int8_t generateBCC2(u_int8_t* data, int dataSize) {
  u_int8_t result = data[0];

  for (int i = 1; i < dataSize; i++) {
    result ^= data[i];
  }

  return result;
}

int sendDataFrame(int fd, u_int8_t* data, int dataSize) {
  resetAlarmVariables();

  int res_read = 0, i = 0;
  u_int8_t mem[3];

  u_int8_t controlByte = generateInfoControlByte(s);
  u_int8_t BCC1 = EMISSOR_CMD_ABYTE ^ controlByte;
  u_int8_t BCC2 = generateBCC2(data, dataSize);

  u_int8_t frameHead[4] = {FLAG_BYTE, EMISSOR_CMD_ABYTE, controlByte, BCC1};
  u_int8_t frameTail[2] = {BCC2, FLAG_BYTE};

  State state = START;
  while (state != STOP) {
    if (conta == 3) {
      printf("Communication failed\n");
      return 1;
    }

    if (messageFlag) {
      for (int i =0; i < dataSize; i++) {
        printf("Sending data byte: %x\n", data[i]);
      }
      write(fd, frameHead, 4);
      write(fd, data, dataSize);
      write(fd, frameTail, 2);

      messageFlag = 0;
      state = START;
      alarm(ALARM_INTERVAL);
    }

    if (receive_supervision_machine(&state, fd, RECEPTOR_ANSWER_ABYTE, RR_CONTROL_BYTE(1-s), mem))
      return 1;
  }

  s = 1 - s;
}

int main(int argc, char** argv) {
    int fd,c, res_read = 0;
    struct termios oldtio,newtio;
    u_int8_t buf[255];
    int i, sum = 0, speed = 0;
    
    if ( (argc < 2) || 
  	     ((strcmp("/dev/ttyS0", argv[1])!=0) && 
  	      (strcmp("/dev/ttyS1", argv[1])!=0) )) {
      printf("Usage:\tnserial SerialPort\n\tex: nserial /dev/ttyS1\n");
      exit(1);
    }

    (void)signal(SIGALRM, atende);

  /*
    Open serial port device for reading and writing and not as controlling tty
    because we don't want to get killed if linenoise sends CTRL-C.
  */


    fd = open(argv[1], O_RDWR | O_NOCTTY );
    if (fd <0) {perror(argv[1]); exit(-1); }

    if ( tcgetattr(fd,&oldtio) == -1) { /* save current port settings */
      perror("tcgetattr");
      exit(-1);
    }

    bzero(&newtio, sizeof(newtio));
    newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;

    /* set input mode (non-canonical, no echo,...) */
    newtio.c_lflag = 0;

    newtio.c_cc[VTIME] = 1;   /* unblock after 0.1secs or 1 char received */
    newtio.c_cc[VMIN] = 0;

  /* 
    VTIME e VMIN devem ser alterados de forma a proteger com um temporizador a 
    leitura do(s) pr�ximo(s) caracter(es)
  */

    tcflush(fd, TCIOFLUSH);

    if ( tcsetattr(fd,TCSANOW,&newtio) == -1) {
      perror("tcsetattr");
      exit(-1);
    }

    printf("New termios structure set\n");
    printf("Sending SET...\n");

    if (sendSet(fd)) exit(1);

    u_int8_t data[9] = {0x51, 0x75, 0x65, 0x20, 0x72, 0x65, 0x67, 0x6f, 0x21};
    sendDataFrame(fd, data, 9);
    
    sleep(1); // Avoid changing config before sending data (transmission error)
    if (tcsetattr(fd,TCSANOW,&oldtio) == -1) {
          perror("tcsetattr");
          exit(-1);
    }
    close(fd);
    printf("Done\n");
    return 0;
}
