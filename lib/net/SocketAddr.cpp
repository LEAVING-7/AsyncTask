/* #include "io/net/SocketAddr.hpp"
#include "predefined.hpp"

auto SocketAddrV4::from_sockaddr_in(sockaddr_in &addr) -> SocketAddrV4 {
  return SocketAddrV4(Ipv4Addr{static_cast<u8>(addr.sin_addr.s_addr >> 24),
                               static_cast<u8>(addr.sin_addr.s_addr >> 16),
                               static_cast<u8>(addr.sin_addr.s_addr >> 8),
                               static_cast<u8>(addr.sin_addr.s_addr)},
                      ntohs(addr.sin_port));
}
auto SocketAddrV4::as_sockaddr_in() const -> sockaddr_in {
  sockaddr_in ret{};
  ret.sin_family = AF_INET;
  ret.sin_port = htons(mPort);
  ret.sin_addr = {
      .s_addr = htonl(*(reinterpret_cast<u32 const *>(mAddr.addr.data())))};
  return ret;
}

auto SocketAddr::from_sockaddr(struct sockaddr *addr) -> SocketAddr {
  if (addr->sa_family == AF_INET) {
    auto ipv4 = reinterpret_cast<sockaddr_in *>(addr);
    return {SocketAddrV4::from_sockaddr_in(*ipv4)};
  } else if (addr->sa_family == AF_INET6) {
    UNIMPLEMENTED();
  }
  UNIMPLEMENTED();
}

auto SocketAddr::toString() -> std::string {
  if (isIpv4()) {
    return fmt::format("{}.{}.{}.{}:{}", mIpv4.address().addr[3],
                       mIpv4.address().addr[2], mIpv4.address().addr[1],
                       mIpv4.address().addr[0], mIpv4.port());
  } else if (isIpv6()) {
    UNIMPLEMENTED();
  }
  UNIMPLEMENTED();
}

auto SocketAddrV6::as_sockaddr_in6() const -> sockaddr_in6 {
  UNIMPLEMENTED("SocketAddrV6::as_sockaddr_in6");
  return {};
}
 */