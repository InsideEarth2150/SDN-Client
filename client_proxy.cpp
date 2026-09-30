#define _CRT_SECURE_NO_WARNINGS
#include <iostream>
#include <cstring>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #define SOCKET int
  #define INVALID_SOCKET -1
  #define closesocket close
#endif

#include <steam/steamnetworkingsockets.h>
#include <steam/isteamnetworkingsockets.h>

const char* REMOTE_GNS_SERVER_IP = "203.0.113.10"; // Public Server IP
const uint16_t REMOTE_GNS_PORT = 50000;
const uint16_t LOCAL_UDP_PORT = 27015;             // Port game connects to locally

const char* USER_ID = "DanPlayer1";
const char* USER_TOKEN = "DanPlayer1_MyUltraSecureSecretKey123!"; // Matching Token

ISteamNetworkingSockets *g_pNetworkingSockets = nullptr;
HSteamNetConnection g_hConnection = k_HSteamNetConnection_Invalid;

SOCKET g_udpLocalListener = INVALID_SOCKET;
sockaddr_in g_gameClientAddr;
bool g_gameClientKnown = false;

void OnConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t *pInfo) {
    if (pInfo->m_info.m_eState == k_ESteamNetworkingConnectionState_Connected) {
        std::cout << "[GNS Client] Connected to Relay Server! Sending Authentication Token..." << std::endl;
        
        // Send Auth Header on establish
        std::string authHeader = std::string("AUTH:") + USER_ID + ":" + USER_TOKEN;
        g_pNetworkingSockets->SendMessageToConnection(
            g_hConnection, authHeader.data(), (uint32_t)authHeader.size(), 
            k_nSteamNetworkingSend_Reliable, nullptr
        );
    } else if (pInfo->m_info.m_eState == k_ESteamNetworkingConnectionState_ClosedByPeer ||
               pInfo->m_info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally) {
        std::cout << "[GNS Client] Connection closed or failed: " << pInfo->m_info.m_szEndDebug << std::endl;
    }
}

int main() {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

    // Setup Local UDP Socket to intercept local game traffic
    g_udpLocalListener = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in localBindAddr{};
    localBindAddr.sin_family = AF_INET;
    localBindAddr.sin_port = htons(LOCAL_UDP_PORT);
    inet_pton(AF_INET, "127.0.0.1", &localBindAddr.sin_addr);

    if (bind(g_udpLocalListener, (sockaddr*)&localBindAddr, sizeof(localBindAddr)) < 0) {
        std::cerr << "Failed to bind local UDP port " << LOCAL_UDP_PORT << std::endl;
        return 1;
    }

    // Initialize GameNetworkingSockets
    SteamDatagramErrMsg errMsg;
    if (!GameNetworkingSockets_Init(nullptr, errMsg)) {
        std::cerr << "GNS Init Failed: " << errMsg << std::endl;
        return 1;
    }

    g_pNetworkingSockets = SteamNetworkingSockets();

    SteamNetworkingIPAddr serverAddr;
    serverAddr.Clear();
    serverAddr.ParseString(REMOTE_GNS_SERVER_IP);
    serverAddr.m_port = REMOTE_GNS_PORT;

    SteamNetworkingConfigValue_t opt;
    opt.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, (void*)OnConnectionStatusChanged);

    std::cout << "[GNS Client] Connecting to " << REMOTE_GNS_SERVER_IP << ":" << REMOTE_GNS_PORT << "..." << std::endl;
    g_hConnection = g_pNetworkingSockets->ConnectByIPAddress(serverAddr, 1, &opt);

    std::cout << "[GNS Client] Interceptor active. Point your game to 127.0.0.1:" << LOCAL_UDP_PORT << std::endl;

    while (true) {
        g_pNetworkingSockets->RunCallbacks();

        // 1. Read Raw UDP from Game Client -> Send across GNS Tunnel
        char udpBuf[2048];
        sockaddr_in fromAddr;
        socklen_t fromLen = sizeof(fromAddr);

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(g_udpLocalListener, &readfds);
        timeval tv{0, 1000}; // 1ms

        if (select((int)g_udpLocalListener + 1, &readfds, nullptr, nullptr, &tv) > 0) {
            int bytesRecv = recvfrom(g_udpLocalListener, udpBuf, sizeof(udpBuf), 0, (sockaddr*)&fromAddr, &fromLen);
            if (bytesRecv > 0) {
                if (!g_gameClientKnown) {
                    g_gameClientAddr = fromAddr;
                    g_gameClientKnown = true;
                }
                // Forward raw game UDP payload into encrypted GNS stream
                g_pNetworkingSockets->SendMessageToConnection(
                    g_hConnection, udpBuf, bytesRecv, k_nSteamNetworkingSend_Unreliable, nullptr
                );
            }
        }

        // 2. Read Encrypted GNS Messages from Server -> Send back to Game Client UDP
        ISteamNetworkingMessage *pIncomingMsgs[16];
        int numMsgs = g_pNetworkingSockets->ReceiveMessagesOnConnection(g_hConnection, pIncomingMsgs, 16);

        for (int i = 0; i < numMsgs; ++i) {
            auto *msg = pIncomingMsgs[i];
            if (g_gameClientKnown) {
                sendto(g_udpLocalListener, (const char*)msg->m_pData, msg->m_cbSize, 0,
                       (sockaddr*)&g_gameClientAddr, sizeof(g_gameClientAddr));
            }
            msg->Release();
        }
    }

    GameNetworkingSockets_Kill();
    return 0;
}