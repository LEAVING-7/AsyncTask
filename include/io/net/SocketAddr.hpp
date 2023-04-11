#pragma once
#include "predefined.hpp"

#include <array>
#include <string>
namespace io {
struct Ipv4Addr {
  constexpr Ipv4Addr(u8 a, u8 b, u8 c, u8 d) : addr {d, c, b, a} {}
  std::array<u8, 4> addr;
};

constexpr Ipv4Addr LOCAL_HOST {127, 0, 0, 1};
constexpr Ipv4Addr BROADCAST {255, 255, 255, 255};
constexpr Ipv4Addr ANY {0, 0, 0, 0};

struct Ipv6Addr {
  constexpr Ipv6Addr(u16 a, u16 b, u16 c, u16 d, u16 e, u16 f, u16 g, u16 h) : addr {a, b, c, d, e, f, g, h} {}
  std::array<u16, 8> addr;
};

class SocketAddrV6 {
public:
  constexpr SocketAddrV6(Ipv6Addr addr, u16 port) : mAddr {addr}, mPort {port} {}
  auto address() -> Ipv6Addr& { return mAddr; }
  auto port() -> u16& { return mPort; }

private:
  Ipv6Addr mAddr;
  u16 mPort;
};

class SocketAddrV4 {
public:
  constexpr SocketAddrV4(Ipv4Addr addr, u16 port) : mAddr {addr}, mPort {port} {}

  auto address() -> Ipv4Addr& { return mAddr; }
  auto address() const -> Ipv4Addr const& { return mAddr; }
  auto port() const -> u16 { return mPort; }

private:
  Ipv4Addr mAddr;
  u16 mPort;
};

class SocketAddr {
public:
  enum class FamilyKind {
    Ipv4,
    Ipv6,
  };

  SocketAddr(SocketAddrV4 addr) : mFamily {FamilyKind::Ipv4}, mIpv4 {addr} {}
  SocketAddr(SocketAddrV6 addr) : mFamily {FamilyKind::Ipv6}, mIpv6 {addr} {}

  SocketAddr(SocketAddr const&) = default;
  SocketAddr& operator=(SocketAddr const&) = default;
  SocketAddr(SocketAddr&&) = default;
  SocketAddr& operator=(SocketAddr&&) = default;

  auto isIpv4() const -> bool { return mFamily == FamilyKind::Ipv4; }
  auto isIpv6() const -> bool { return mFamily == FamilyKind::Ipv6; }

  auto getIpv4() const -> SocketAddrV4 const& { return mIpv4; }
  auto getIpv6() const -> SocketAddrV6 const& { return mIpv6; }

  auto getIpv4() -> SocketAddrV4& { return mIpv4; }
  auto getIpv6() -> SocketAddrV6& { return mIpv6; }

  // static auto from_sockaddr(struct sockaddr *addr) -> SocketAddr;

  auto toString() -> std::string;

private:
  FamilyKind mFamily;
  union {
    SocketAddrV4 mIpv4;
    SocketAddrV6 mIpv6;
  };
};
} // namespace io