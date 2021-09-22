#define MQTTCLIENT_QOS2 1

#include <memory.h>

#include "MQTTClient.h"

#define DEFAULT_STACK_SIZE -1

#include "linux.cpp"

// Linux headers
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <termios.h>


int arrivedcount = 0;
bool quit = false;

class Serial
{
  int fd = 0;

public:
  Serial(const char *port)
  {
    // Abre a porta serial
    fd = open(port, O_RDWR | O_NOCTTY);
    if(fd < 0)
      printf("Erro ao abrir porta\n");
  }

  ~Serial()
  {
    printf("Destrutor da classe Serial\n");
  }

  void init()
  {
    // Armazenar a configuração atual da porta serial
    struct termios options;
    int result = tcgetattr(fd, &options);
    if (result)
    {
      printf("tcgetattr: falha ao ler a configuração atual da porta serial\n");
      close(fd);
    }

    // Desabilitar qualquer opção desnecessária para enviar e receber bytes
    options.c_iflag &= ~(INLCR | IGNCR | ICRNL | IXON | IXOFF);
    options.c_oflag &= ~(ONLCR | OCRNL);
    options.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);

    // Configurar timeouts: Chamadas para read() irão retornar assim que
    // houver pelo menos 1 byte disponível ou após 100 ms
    options.c_cc[VTIME] = 1;
    options.c_cc[VMIN] = 0;

    // Configurando velocidade em 9600 bps
    cfsetospeed(&options, B9600);
    cfsetispeed(&options, B9600);

    // Configurando interface serial
    result = tcsetattr(fd, TCSANOW, &options);
    if (result)
    {
      perror("tcsetattr: falha ao configurar interface serial\n");
      close(fd);
    }
  }

  int send(uint8_t *buffer, size_t size)
  {
    if(fd < 0)
      return -1;

    ssize_t result = write(fd, buffer, size);
    if(result != (ssize_t)size)
    {
      printf("falha ao enviar bytes\n");
      return -1;
    }
    return 0;
  }
};

const char serialPort[] = "/dev/ttyUSB0";
Serial serial(serialPort);

void sendPack(int v1_, int v2_, int v3_)
{
  unsigned char ledA = 0, ledB = 0, pwm = 0;

  if(v1_ > 0)
    ledA = 1;

  if(v2_ > 0)
    ledB = 1;

  pwm = (v3_ > 0x64) ? 0x64 : v3_;

  unsigned char pack[5] = {0x00, ledA, ledB, pwm, 0xFF};
  printf("enviando pacote: ");
  for(unsigned idx = 0; idx < 5; ++idx)
    printf("%x ", pack[idx]);
  printf("\n");

  serial.send(pack, sizeof(pack));
}

void analyze(MQTT::Message& message_)
{
  MQTT::Message &message = message_;

  // Declara ponteiro para ler as mensagens recebidas
  // e aloca memória
  char *msg = (char*) malloc(message.payloadlen + 1);

  // Limpa a memória
  memset(msg, 0x00, message.payloadlen + 1);

  // Copia o conteúdo recebido para a memória
  memcpy(msg, message.payload, message.payloadlen);

  // Variáveis auxiliares
  int result = 0, v1 = 0, v2 = 0, v3 = 0;

  // Atribui os valores recebidos para as variáveis auxiliares
  result = sscanf(msg, "%d %d %d", &v1, &v2, &v3);

  if(strcmp(msg, "quit") == 0)
  {
    // A mensagem "quit" finaliza o programa
    printf("O programa irá finalizar\n");
    quit = true;
  }
  else if(result == 3)  // Se três valores forem adequadamente convertidos
    sendPack(v1, v2, v3);  // Formata um pacote para enviar pela serial

  free(msg); // Desaloca a memória
}

void messageArrived(MQTT::MessageData& md)
{
    MQTT::Message &message = md.message;

    printf("Message %d arrived: qos %d, retained %d, dup %d, packetid %d\n", 
		++arrivedcount, message.qos, message.retained, message.dup, message.id);
    printf("Payload %.*s\n", (int)message.payloadlen, (char*)message.payload);

    analyze(message);
}


int main(int argc, char* argv[])
{   
    quit = false;

    IPStack ipstack = IPStack();
    float version = 0.3;
    const char* topic = "2ELE069";
    
    printf("Version is %f\n", version);
              
    MQTT::Client<IPStack, Countdown> client = MQTT::Client<IPStack, Countdown>(ipstack);
    
    // const char* hostname = "iot.eclipse.org";
    const char* hostname = "broker.hivemq.com";
    int port = 1883;
    printf("Connecting to %s:%d\n", hostname, port);
    int rc = ipstack.connect(hostname, port);
	if (rc != 0)
	    printf("rc from TCP connect is %d\n", rc);
 
	printf("MQTT connecting\n");
    MQTTPacket_connectData data = MQTTPacket_connectData_initializer;       
    data.MQTTVersion = 3;
    data.clientID.cstring = (char*)"mbed-icraggs";
    rc = client.connect(data);
	if (rc != 0)
	    printf("rc from MQTT connect is %d\n", rc);
	printf("MQTT connected\n");
    
    rc = client.subscribe(topic, MQTT::QOS2, messageArrived);   
    if (rc != 0)
        printf("rc from MQTT subscribe is %d\n", rc);

    MQTT::Message message;

    // QoS 0
    char buf[100];
    sprintf(buf, "Hello World!  QoS 0 message from app version %f", version);
    message.qos = MQTT::QOS0;
    message.retained = false;
    message.dup = false;
    message.payload = (void*)buf;
    message.payloadlen = strlen(buf)+1;
    rc = client.publish(topic, message);
	if (rc != 0)
		printf("Error %d from sending QoS 0 message\n", rc);
    else while (arrivedcount == 0)
        client.yield(100);
        
    // QoS 1
	printf("Now QoS 1\n");
    sprintf(buf, "Hello World!  QoS 1 message from app version %f", version);
    message.qos = MQTT::QOS1;
    message.payloadlen = strlen(buf)+1;
    rc = client.publish(topic, message);
	if (rc != 0)
		printf("Error %d from sending QoS 1 message\n", rc);
    else while (arrivedcount == 1)
        client.yield(100);
        
    // QoS 2
    sprintf(buf, "Hello World!  QoS 2 message from app version %f", version);
    message.qos = MQTT::QOS2;
    message.payloadlen = strlen(buf)+1;
    rc = client.publish(topic, message);
	if (rc != 0)
		printf("Error %d from sending QoS 2 message\n", rc);
    while (arrivedcount == 2)
        client.yield(100);
    
    while(!quit)
    {
        client.yield(1);
    }

    rc = client.unsubscribe(topic);
    if (rc != 0)
        printf("rc from unsubscribe was %d\n", rc);
    
    rc = client.disconnect();
    if (rc != 0)
        printf("rc from disconnect was %d\n", rc);
    
    ipstack.disconnect();
    
    printf("Finishing with %d messages received\n", arrivedcount);
    
    return 0;
}

