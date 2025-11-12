export module tskv.net.utils;

export namespace tskv::net {

inline bool is_valid_port(auto port)
{
  return port >= 1 && port <= 65535;
}

} // namespace tskv::net
