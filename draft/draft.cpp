#include <mutex>

#include "io/net/SocketAddr.hpp"
#include "io/sys/common/tcp.hpp"

int main(int argc, char* argv[])
{
  auto listener = io::TcpListener::Bind(io::SocketAddrV4 {io::LOCAL_HOST, 8080});
  if (!listener) {
    return 1;
  }
  auto stream = listener->accept(nullptr);
  if (!stream) {
    return 1;
  }
  char buf[1024];
  auto result = stream->read(buf, 1024);
  if (!result) {
    return 1;
  }
  buf[1023] = '\0';
  printf("%s", buf);

  return 0;
}