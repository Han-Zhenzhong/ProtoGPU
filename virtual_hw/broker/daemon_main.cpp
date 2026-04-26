#include "protogpu/broker_transport.h"

#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
  std::string socket_path = "/tmp/protogpu-broker.sock";
  if (argc > 1) {
    socket_path = argv[1];
  } else if (const char* env = std::getenv("PROTOGPU_BROKER_SOCK")) {
    if (*env != '\0') socket_path = env;
  }

  std::cout << "protogpu-vhw-brokerd listening on " << socket_path << "\n";
  return protogpu::run_broker_daemon(socket_path);
}
