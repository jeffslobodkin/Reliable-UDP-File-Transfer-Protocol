#define MAX_PKT_SIZE (1500-28)

#pragma pack(push, 1)
class Packet {
	public:
		int type; // SYN, FIN, data
		int size; // bytes in packet data
		clock_t txTime; // transmission time
		char pkt[MAX_PKT_SIZE]; // packet with header
};
#pragma pack(pop)