#pragma pack(push,1) 
class SenderDataHeader {
public:
	Flags flags;
	DWORD seq; // must begin from 0
};
#pragma pack(pop)