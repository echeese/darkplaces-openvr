/*
Copyright (C) 1996-1997 Id Software, Inc.
Copyright (C) 2002 Mathieu Olivier
Copyright (C) 2003 Forest Hale

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "quakedef.h"
#include "lhnet.h"

#define MASTER_PORT 27950

// note this defaults on for dedicated servers, off for listen servers
cvar_t sv_public = {0, "sv_public", "0", "advertises this server on the master server (so that players can find it in the server browser)"};
static cvar_t sv_heartbeatperiod = {CVAR_SAVE, "sv_heartbeatperiod", "120", "how often to send heartbeat in seconds (only used if sv_public is 1)"};

// FIXME: resolve DNS on masters whenever their value changes and cache it (to avoid major delays in active servers when they heartbeat)
static cvar_t sv_masters [] =
{
	{CVAR_SAVE, "sv_master1", "", "user-chosen master server 1"},
	{CVAR_SAVE, "sv_master2", "", "user-chosen master server 2"},
	{CVAR_SAVE, "sv_master3", "", "user-chosen master server 3"},
	{CVAR_SAVE, "sv_master4", "", "user-chosen master server 4"},
	{0, "sv_masterextra1", "ghdigital.com", "default master server 1 (admin: LordHavoc)"}, // admin: LordHavoc
	{0, "sv_masterextra2", "dpmaster.deathmask.net", "default master server 2 (admin: Willis)"}, // admin: Willis
	{0, "sv_masterextra3", "12.166.196.192", "default master server 3 (admin: Venim)"}, // admin: Venim
	{0, "sv_masterextra4", "excalibur.nvg.ntnu.no", "default master server 4 (admin: tChr)"}, // admin: tChr
	{0, NULL, NULL, NULL}
};

static double nextheartbeattime = 0;

sizebuf_t net_message;
static unsigned char net_message_buf[NET_MAXMESSAGE];

cvar_t net_messagetimeout = {0, "net_messagetimeout","300", "drops players who have not sent any packets for this many seconds"};
cvar_t net_messagerejointimeout = {0, "net_messagerejointimeout","10", "give a player this much time in seconds to rejoin and continue playing (not losing frags and such)"};
cvar_t net_connecttimeout = {0, "net_connecttimeout","10", "after requesting a connection, the client must reply within this many seconds or be dropped (cuts down on connect floods)"};
cvar_t hostname = {CVAR_SAVE, "hostname", "UNNAMED", "server message to show in server browser"};
cvar_t developer_networking = {0, "developer_networking", "0", "prints all received and sent packets (recommended only for debugging)"};

cvar_t cl_netlocalping = {0, "cl_netlocalping","0", "lags local loopback connection by this much ping time (useful to play more fairly on your own server with people with higher pings)"};
static cvar_t cl_netpacketloss = {0, "cl_netpacketloss","0", "drops this percentage of packets (incoming and outgoing), useful for testing network protocol robustness (effects failing to start, sounds failing to play, etc)"};
static cvar_t net_slist_queriespersecond = {0, "net_slist_queriespersecond", "20", "how many server information requests to send per second"};
static cvar_t net_slist_queriesperframe = {0, "net_slist_queriesperframe", "4", "maximum number of server information requests to send each rendered frame (guards against low framerates causing problems)"};
static cvar_t net_slist_timeout = {0, "net_slist_timeout", "4", "how long to listen for a server information response before giving up"};
static cvar_t net_slist_maxtries = {0, "net_slist_maxtries", "3", "how many times to ask the same server for information (more times gives better ping reports but takes longer)"};

/* statistic counters */
static int packetsSent = 0;
static int packetsReSent = 0;
static int packetsReceived = 0;
static int receivedDuplicateCount = 0;
static int droppedDatagrams = 0;

static int unreliableMessagesSent = 0;
static int unreliableMessagesReceived = 0;
static int reliableMessagesSent = 0;
static int reliableMessagesReceived = 0;

double masterquerytime = -1000;
int masterquerycount = 0;
int masterreplycount = 0;
int serverquerycount = 0;
int serverreplycount = 0;

// this is only false if there are still servers left to query
int serverlist_querysleep = true;

static unsigned char sendbuffer[NET_HEADERSIZE+NET_MAXMESSAGE];
static unsigned char readbuffer[NET_HEADERSIZE+NET_MAXMESSAGE];

int cl_numsockets;
lhnetsocket_t *cl_sockets[16];
int sv_numsockets;
lhnetsocket_t *sv_sockets[16];

netconn_t *netconn_list = NULL;
mempool_t *netconn_mempool = NULL;

cvar_t cl_netport = {0, "cl_port", "0", "forces client to use chosen port number if not 0"};
cvar_t sv_netport = {0, "port", "26000", "server port for players to connect to"};
cvar_t net_address = {0, "net_address", "0.0.0.0", "network address to open ports on"};
//cvar_t net_netaddress_ipv6 = {0, "net_address_ipv6", "[0:0:0:0:0:0:0:0]", "network address to open ipv6 ports on"};

// ServerList interface
serverlist_mask_t serverlist_andmasks[SERVERLIST_ANDMASKCOUNT];
serverlist_mask_t serverlist_ormasks[SERVERLIST_ORMASKCOUNT];

serverlist_infofield_t serverlist_sortbyfield;
qboolean serverlist_sortdescending;

int serverlist_viewcount = 0;
serverlist_entry_t *serverlist_viewlist[SERVERLIST_VIEWLISTSIZE];

int serverlist_cachecount;
serverlist_entry_t serverlist_cache[SERVERLIST_TOTALSIZE];

qboolean serverlist_consoleoutput;

// helper function to insert a value into the viewset
// spare entries will be removed
static void _ServerList_ViewList_Helper_InsertBefore( int index, serverlist_entry_t *entry )
{
    int i;
	if( serverlist_viewcount < SERVERLIST_VIEWLISTSIZE ) {
		i = serverlist_viewcount++;
	} else {
		i = SERVERLIST_VIEWLISTSIZE - 1;
	}

	for( ; i > index ; i-- )
		serverlist_viewlist[ i ] = serverlist_viewlist[ i - 1 ];

	serverlist_viewlist[index] = entry;
}

// we suppose serverlist_viewcount to be valid, ie > 0
static void _ServerList_ViewList_Helper_Remove( int index )
{
	serverlist_viewcount--;
	for( ; index < serverlist_viewcount ; index++ )
		serverlist_viewlist[index] = serverlist_viewlist[index + 1];
}

// returns true if A should be inserted before B
static qboolean _ServerList_Entry_Compare( serverlist_entry_t *A, serverlist_entry_t *B )
{
	int result = 0; // > 0 if for numbers A > B and for text if A < B

	switch( serverlist_sortbyfield ) {
		case SLIF_PING:
			result = A->info.ping - B->info.ping;
			break;
		case SLIF_MAXPLAYERS:
			result = A->info.maxplayers - B->info.maxplayers;
			break;
		case SLIF_NUMPLAYERS:
			result = A->info.numplayers - B->info.numplayers;
			break;
		case SLIF_PROTOCOL:
			result = A->info.protocol - B->info.protocol;
			break;
		case SLIF_CNAME:
			result = strcmp( B->info.cname, A->info.cname );
			break;
		case SLIF_GAME:
			result = strcmp( B->info.game, A->info.game );
			break;
		case SLIF_MAP:
			result = strcmp( B->info.map, A->info.map );
			break;
		case SLIF_MOD:
			result = strcmp( B->info.mod, A->info.mod );
			break;
		case SLIF_NAME:
			result = strcmp( B->info.name, A->info.name );
			break;
		default:
			Con_DPrint( "_ServerList_Entry_Compare: Bad serverlist_sortbyfield!\n" );
			break;
	}

	if( serverlist_sortdescending )
		return result > 0;
	return result < 0;
}

static qboolean _ServerList_CompareInt( int A, serverlist_maskop_t op, int B )
{
	// This should actually be done with some intermediate and end-of-function return
	switch( op ) {
		case SLMO_LESS:
			return A < B;
		case SLMO_LESSEQUAL:
			return A <= B;
		case SLMO_EQUAL:
			return A == B;
		case SLMO_GREATER:
			return A > B;
		case SLMO_NOTEQUAL:
			return A != B;
		case SLMO_GREATEREQUAL:
		case SLMO_CONTAINS:
		case SLMO_NOTCONTAIN:
			return A >= B;
		default:
			Con_DPrint( "_ServerList_CompareInt: Bad op!\n" );
			return false;
	}
}

static qboolean _ServerList_CompareStr( const char *A, serverlist_maskop_t op, const char *B )
{
	int i;
	char bufferA[ 256 ], bufferB[ 256 ]; // should be more than enough
	for (i = 0;i < (int)sizeof(bufferA)-1 && A[i];i++)
		bufferA[i] = (A[i] >= 'A' && A[i] <= 'Z') ? (A[i] + 'a' - 'A') : A[i];
	bufferA[i] = 0;
	for (i = 0;i < (int)sizeof(bufferB)-1 && B[i];i++)
		bufferB[i] = (B[i] >= 'A' && B[i] <= 'Z') ? (B[i] + 'a' - 'A') : B[i];
	bufferB[i] = 0;

	// Same here, also using an intermediate & final return would be more appropriate
	// A info B mask
	switch( op ) {
		case SLMO_CONTAINS:
			return *bufferB && !!strstr( bufferA, bufferB ); // we want a real bool
		case SLMO_NOTCONTAIN:
			return !*bufferB || !strstr( bufferA, bufferB );
		case SLMO_LESS:
			return strcmp( bufferA, bufferB ) < 0;
		case SLMO_LESSEQUAL:
			return strcmp( bufferA, bufferB ) <= 0;
		case SLMO_EQUAL:
			return strcmp( bufferA, bufferB ) == 0;
		case SLMO_GREATER:
			return strcmp( bufferA, bufferB ) > 0;
		case SLMO_NOTEQUAL:
			return strcmp( bufferA, bufferB ) != 0;
		case SLMO_GREATEREQUAL:
			return strcmp( bufferA, bufferB ) >= 0;
		default:
			Con_DPrint( "_ServerList_CompareStr: Bad op!\n" );
			return false;
	}
}

static qboolean _ServerList_Entry_Mask( serverlist_mask_t *mask, serverlist_info_t *info )
{
	if( !_ServerList_CompareInt( info->ping, mask->tests[SLIF_PING], mask->info.ping ) )
		return false;
	if( !_ServerList_CompareInt( info->maxplayers, mask->tests[SLIF_MAXPLAYERS], mask->info.maxplayers ) )
		return false;
	if( !_ServerList_CompareInt( info->numplayers, mask->tests[SLIF_NUMPLAYERS], mask->info.numplayers ) )
		return false;
	if( !_ServerList_CompareInt( info->protocol, mask->tests[SLIF_PROTOCOL], mask->info.protocol ))
		return false;
	if( *mask->info.cname
		&& !_ServerList_CompareStr( info->cname, mask->tests[SLIF_CNAME], mask->info.cname ) )
		return false;
	if( *mask->info.game
		&& !_ServerList_CompareStr( info->game, mask->tests[SLIF_GAME], mask->info.game ) )
		return false;
	if( *mask->info.mod
		&& !_ServerList_CompareStr( info->mod, mask->tests[SLIF_MOD], mask->info.mod ) )
		return false;
	if( *mask->info.map
		&& !_ServerList_CompareStr( info->map, mask->tests[SLIF_MAP], mask->info.map ) )
		return false;
	if( *mask->info.name
		&& !_ServerList_CompareStr( info->name, mask->tests[SLIF_NAME], mask->info.name ) )
		return false;
	return true;
}

static void ServerList_ViewList_Insert( serverlist_entry_t *entry )
{
	int start, end, mid;

	// FIXME: change this to be more readable (...)
	// now check whether it passes through the masks
	for( start = 0 ; serverlist_andmasks[start].active && start < SERVERLIST_ANDMASKCOUNT ; start++ )
		if( !_ServerList_Entry_Mask( &serverlist_andmasks[start], &entry->info ) )
			return;

	for( start = 0 ; serverlist_ormasks[start].active && start < SERVERLIST_ORMASKCOUNT ; start++ )
		if( _ServerList_Entry_Mask( &serverlist_ormasks[start], &entry->info ) )
			break;
	if( start == SERVERLIST_ORMASKCOUNT || (start > 0 && !serverlist_ormasks[start].active) )
		return;

	if( !serverlist_viewcount ) {
		_ServerList_ViewList_Helper_InsertBefore( 0, entry );
		return;
	}
	// ok, insert it, we just need to find out where exactly:

	// two special cases
	// check whether to insert it as new first item
	if( _ServerList_Entry_Compare( entry, serverlist_viewlist[0] ) ) {
		_ServerList_ViewList_Helper_InsertBefore( 0, entry );
		return;
	} // check whether to insert it as new last item
	else if( !_ServerList_Entry_Compare( entry, serverlist_viewlist[serverlist_viewcount - 1] ) ) {
		_ServerList_ViewList_Helper_InsertBefore( serverlist_viewcount, entry );
		return;
	}
	start = 0;
	end = serverlist_viewcount - 1;
	while( end > start + 1 )
	{
		mid = (start + end) / 2;
		// test the item that lies in the middle between start and end
		if( _ServerList_Entry_Compare( entry, serverlist_viewlist[mid] ) )
			// the item has to be in the upper half
			end = mid;
		else
			// the item has to be in the lower half
			start = mid;
	}
	_ServerList_ViewList_Helper_InsertBefore( start + 1, entry );
}

static void ServerList_ViewList_Remove( serverlist_entry_t *entry )
{
	int i;
	for( i = 0; i < serverlist_viewcount; i++ )
	{
		if (serverlist_viewlist[i] == entry)
		{
			_ServerList_ViewList_Helper_Remove(i);
			break;
		}
	}
}

void ServerList_RebuildViewList(void)
{
	int i;

	serverlist_viewcount = 0;
	for( i = 0 ; i < serverlist_cachecount ; i++ )
		if( serverlist_cache[i].query == SQS_QUERIED )
			ServerList_ViewList_Insert( &serverlist_cache[i] );
}

void ServerList_ResetMasks(void)
{
	memset( &serverlist_andmasks, 0, sizeof( serverlist_andmasks ) );
	memset( &serverlist_ormasks, 0, sizeof( serverlist_ormasks ) );
}

#if 0
static void _ServerList_Test(void)
{
	int i;
	for( i = 0 ; i < 1024 ; i++ ) {
		memset( &serverlist_cache[serverlist_cachecount], 0, sizeof( serverlist_entry_t ) );
		serverlist_cache[serverlist_cachecount].info.ping = 1000 + 1024 - i;
		dpsnprintf( serverlist_cache[serverlist_cachecount].info.name, sizeof(serverlist_cache[serverlist_cachecount].info.name), "Black's ServerList Test %i", i );
		serverlist_cache[serverlist_cachecount].finished = true;
		sprintf( serverlist_cache[serverlist_cachecount].line1, "%i %s", serverlist_cache[serverlist_cachecount].info.ping, serverlist_cache[serverlist_cachecount].info.name );
		ServerList_ViewList_Insert( &serverlist_cache[serverlist_cachecount] );
		serverlist_cachecount++;
	}
}
#endif

void ServerList_QueryList(void)
{
	masterquerytime = realtime;
	masterquerycount = 0;
	masterreplycount = 0;
	serverquerycount = 0;
	serverreplycount = 0;
	serverlist_cachecount = 0;
	serverlist_viewcount = 0;
	serverlist_consoleoutput = false;

	//_ServerList_Test();

	NetConn_QueryMasters();
}

// rest

int NetConn_Read(lhnetsocket_t *mysocket, void *data, int maxlength, lhnetaddress_t *peeraddress)
{
	int length = LHNET_Read(mysocket, data, maxlength, peeraddress);
	int i;
	if (length == 0)
		return 0;
	if (cl_netpacketloss.integer)
		for (i = 0;i < cl_numsockets;i++)
			if (cl_sockets[i] == mysocket && (rand() % 100) < cl_netpacketloss.integer)
				return 0;
	if (developer_networking.integer)
	{
		char addressstring[128], addressstring2[128];
		LHNETADDRESS_ToString(LHNET_AddressFromSocket(mysocket), addressstring, sizeof(addressstring), true);
		if (length > 0)
		{
			LHNETADDRESS_ToString(peeraddress, addressstring2, sizeof(addressstring2), true);
			Con_Printf("LHNET_Read(%p (%s), %p, %i, %p) = %i from %s:\n", mysocket, addressstring, data, maxlength, peeraddress, length, addressstring2);
			Com_HexDumpToConsole((unsigned char *)data, length);
		}
		else
			Con_Printf("LHNET_Read(%p (%s), %p, %i, %p) = %i\n", mysocket, addressstring, data, maxlength, peeraddress, length);
	}
	return length;
}

int NetConn_Write(lhnetsocket_t *mysocket, const void *data, int length, const lhnetaddress_t *peeraddress)
{
	int ret;
	int i;
	if (cl_netpacketloss.integer)
		for (i = 0;i < cl_numsockets;i++)
			if (cl_sockets[i] == mysocket && (rand() % 100) < cl_netpacketloss.integer)
				return length;
	ret = LHNET_Write(mysocket, data, length, peeraddress);
	if (developer_networking.integer)
	{
		char addressstring[128], addressstring2[128];
		LHNETADDRESS_ToString(LHNET_AddressFromSocket(mysocket), addressstring, sizeof(addressstring), true);
		LHNETADDRESS_ToString(peeraddress, addressstring2, sizeof(addressstring2), true);
		Con_Printf("LHNET_Write(%p (%s), %p, %i, %p (%s)) = %i%s\n", mysocket, addressstring, data, length, peeraddress, addressstring2, length, ret == length ? "" : " (ERROR)");
		Com_HexDumpToConsole((unsigned char *)data, length);
	}
	return ret;
}

int NetConn_WriteString(lhnetsocket_t *mysocket, const char *string, const lhnetaddress_t *peeraddress)
{
	// note this does not include the trailing NULL because we add that in the parser
	return NetConn_Write(mysocket, string, (int)strlen(string), peeraddress);
}

int NetConn_SendReliableMessage(netconn_t *conn, sizebuf_t *data)
{
	unsigned int packetLen;
	unsigned int dataLen;
	unsigned int eom;
	unsigned int *header;

//#ifdef DEBUG
	if (data->cursize == 0)
	{
		Con_Printf ("Datagram_SendMessage: zero length message\n");
		return -1;
	}

	if (data->cursize > (int)sizeof(conn->sendMessage))
	{
		Con_Printf ("Datagram_SendMessage: message too big (%u > %u)\n", data->cursize, sizeof(conn->sendMessage));
		return -1;
	}

	if (conn->canSend == false)
	{
		Con_Printf ("SendMessage: called with canSend == false\n");
		return -1;
	}
//#endif

	memcpy(conn->sendMessage, data->data, data->cursize);
	conn->sendMessageLength = data->cursize;

	if (conn->sendMessageLength <= MAX_PACKETFRAGMENT)
	{
		dataLen = conn->sendMessageLength;
		eom = NETFLAG_EOM;
	}
	else
	{
		dataLen = MAX_PACKETFRAGMENT;
		eom = 0;
	}

	packetLen = NET_HEADERSIZE + dataLen;

	header = (unsigned int *)sendbuffer;
	header[0] = BigLong(packetLen | (NETFLAG_DATA | eom));
	header[1] = BigLong(conn->sendSequence);
	memcpy(sendbuffer + NET_HEADERSIZE, conn->sendMessage, dataLen);

	conn->sendSequence++;
	conn->canSend = false;

	if (NetConn_Write(conn->mysocket, (void *)&sendbuffer, packetLen, &conn->peeraddress) != (int)packetLen)
		return -1;

	conn->lastSendTime = realtime;
	packetsSent++;
	reliableMessagesSent++;
	return 1;
}

static void NetConn_SendMessageNext(netconn_t *conn)
{
	unsigned int packetLen;
	unsigned int dataLen;
	unsigned int eom;
	unsigned int *header;

	if (conn->sendMessageLength && !conn->canSend && conn->sendNext)
	{
		if (conn->sendMessageLength <= MAX_PACKETFRAGMENT)
		{
			dataLen = conn->sendMessageLength;
			eom = NETFLAG_EOM;
		}
		else
		{
			dataLen = MAX_PACKETFRAGMENT;
			eom = 0;
		}

		packetLen = NET_HEADERSIZE + dataLen;

		header = (unsigned int *)sendbuffer;
		header[0] = BigLong(packetLen | (NETFLAG_DATA | eom));
		header[1] = BigLong(conn->sendSequence);
		memcpy(sendbuffer + NET_HEADERSIZE, conn->sendMessage, dataLen);

		conn->sendSequence++;
		conn->sendNext = false;

		if (NetConn_Write(conn->mysocket, (void *)&sendbuffer, packetLen, &conn->peeraddress) != (int)packetLen)
			return;

		conn->lastSendTime = realtime;
		packetsSent++;
	}
}

static void NetConn_ReSendMessage(netconn_t *conn)
{
	unsigned int packetLen;
	unsigned int dataLen;
	unsigned int eom;
	unsigned int *header;

	if (conn->sendMessageLength && !conn->canSend && (realtime - conn->lastSendTime) > 1.0)
	{
		if (conn->sendMessageLength <= MAX_PACKETFRAGMENT)
		{
			dataLen = conn->sendMessageLength;
			eom = NETFLAG_EOM;
		}
		else
		{
			dataLen = MAX_PACKETFRAGMENT;
			eom = 0;
		}

		packetLen = NET_HEADERSIZE + dataLen;

		header = (unsigned int *)sendbuffer;
		header[0] = BigLong(packetLen | (NETFLAG_DATA | eom));
		header[1] = BigLong(conn->sendSequence - 1);
		memcpy(sendbuffer + NET_HEADERSIZE, conn->sendMessage, dataLen);

		conn->sendNext = false;

		if (NetConn_Write(conn->mysocket, (void *)&sendbuffer, packetLen, &conn->peeraddress) != (int)packetLen)
			return;

		conn->lastSendTime = realtime;
		packetsReSent++;
	}
}

qboolean NetConn_CanSendMessage(netconn_t *conn)
{
	return conn->canSend;
}

int NetConn_SendUnreliableMessage(netconn_t *conn, sizebuf_t *data)
{
	int packetLen;
	unsigned int *header;

	packetLen = NET_HEADERSIZE + data->cursize;

//#ifdef DEBUG
	if (data->cursize == 0)
	{
		Con_Printf ("Datagram_SendUnreliableMessage: zero length message\n");
		return -1;
	}

	if (packetLen > (int)sizeof(sendbuffer))
	{
		Con_Printf ("Datagram_SendUnreliableMessage: message too big %u\n", data->cursize);
		return -1;
	}
//#endif

	header = (unsigned int *)sendbuffer;
	header[0] = BigLong(packetLen | NETFLAG_UNRELIABLE);
	header[1] = BigLong(conn->unreliableSendSequence);
	memcpy(sendbuffer + NET_HEADERSIZE, data->data, data->cursize);

	conn->unreliableSendSequence++;

	if (NetConn_Write(conn->mysocket, (void *)&sendbuffer, packetLen, &conn->peeraddress) != (int)packetLen)
		return -1;

	packetsSent++;
	unreliableMessagesSent++;
	return 1;
}

void NetConn_CloseClientPorts(void)
{
	for (;cl_numsockets > 0;cl_numsockets--)
		if (cl_sockets[cl_numsockets - 1])
			LHNET_CloseSocket(cl_sockets[cl_numsockets - 1]);
}

void NetConn_OpenClientPort(const char *addressstring, int defaultport)
{
	lhnetaddress_t address;
	lhnetsocket_t *s;
	char addressstring2[1024];
	if (LHNETADDRESS_FromString(&address, addressstring, defaultport))
	{
		if ((s = LHNET_OpenSocket_Connectionless(&address)))
		{
			cl_sockets[cl_numsockets++] = s;
			LHNETADDRESS_ToString(LHNET_AddressFromSocket(s), addressstring2, sizeof(addressstring2), true);
			Con_Printf("Client opened a socket on address %s\n", addressstring2);
		}
		else
		{
			LHNETADDRESS_ToString(&address, addressstring2, sizeof(addressstring2), true);
			Con_Printf("Client failed to open a socket on address %s\n", addressstring2);
		}
	}
	else
		Con_Printf("Client unable to parse address %s\n", addressstring);
}

void NetConn_OpenClientPorts(void)
{
	int port;
	NetConn_CloseClientPorts();
	port = bound(0, cl_netport.integer, 65535);
	if (cl_netport.integer != port)
		Cvar_SetValueQuick(&cl_netport, port);
	Con_Printf("Client using port %i\n", port);
	NetConn_OpenClientPort("local:2", 0);
	NetConn_OpenClientPort(net_address.string, port);
	//NetConn_OpenClientPort(net_address_ipv6.string, port);
}

void NetConn_CloseServerPorts(void)
{
	for (;sv_numsockets > 0;sv_numsockets--)
		if (sv_sockets[sv_numsockets - 1])
			LHNET_CloseSocket(sv_sockets[sv_numsockets - 1]);
}

void NetConn_OpenServerPort(const char *addressstring, int defaultport)
{
	lhnetaddress_t address;
	lhnetsocket_t *s;
	int port;
	char addressstring2[1024];

	for (port = defaultport; port <= defaultport + 100; port++)
	{
		if (LHNETADDRESS_FromString(&address, addressstring, port))
		{
			if ((s = LHNET_OpenSocket_Connectionless(&address)))
			{
				sv_sockets[sv_numsockets++] = s;
				LHNETADDRESS_ToString(LHNET_AddressFromSocket(s), addressstring2, sizeof(addressstring2), true);
				Con_Printf("Server listening on address %s\n", addressstring2);
				break;
			}
			else
			{
				LHNETADDRESS_ToString(&address, addressstring2, sizeof(addressstring2), true);
				Con_Printf("Server failed to open socket on address %s\n", addressstring2);
			}
		}
		else
		{
			Con_Printf("Server unable to parse address %s\n", addressstring);
			// if it cant parse one address, it wont be able to parse another for sure
			break;
		}
	}
}

static void NetConn_UpdateServerStuff(void);
void NetConn_OpenServerPorts(int opennetports)
{
	int port;
	NetConn_CloseServerPorts();
	NetConn_UpdateServerStuff();
	port = bound(0, sv_netport.integer, 65535);
	if (port == 0)
		port = 26000;
	Con_Printf("Server using port %i\n", port);
	if (sv_netport.integer != port)
		Cvar_SetValueQuick(&sv_netport, port);
	if (cls.state != ca_dedicated)
		NetConn_OpenServerPort("local:1", 0);
	if (opennetports)
	{
		NetConn_OpenServerPort(net_address.string, port);
		//NetConn_OpenServerPort(net_address_ipv6.string, port);
	}
	if (sv_numsockets == 0)
		Host_Error("NetConn_OpenServerPorts: unable to open any ports!");
}

lhnetsocket_t *NetConn_ChooseClientSocketForAddress(lhnetaddress_t *address)
{
	int i, a = LHNETADDRESS_GetAddressType(address);
	for (i = 0;i < cl_numsockets;i++)
		if (cl_sockets[i] && LHNETADDRESS_GetAddressType(LHNET_AddressFromSocket(cl_sockets[i])) == a)
			return cl_sockets[i];
	return NULL;
}

lhnetsocket_t *NetConn_ChooseServerSocketForAddress(lhnetaddress_t *address)
{
	int i, a = LHNETADDRESS_GetAddressType(address);
	for (i = 0;i < sv_numsockets;i++)
		if (sv_sockets[i] && LHNETADDRESS_GetAddressType(LHNET_AddressFromSocket(sv_sockets[i])) == a)
			return sv_sockets[i];
	return NULL;
}

netconn_t *NetConn_Open(lhnetsocket_t *mysocket, lhnetaddress_t *peeraddress)
{
	netconn_t *conn;
	conn = (netconn_t *)Mem_Alloc(netconn_mempool, sizeof(*conn));
	conn->mysocket = mysocket;
	conn->peeraddress = *peeraddress;
	conn->canSend = true;
	conn->lastMessageTime = realtime;
	conn->message.data = conn->messagedata;
	conn->message.maxsize = sizeof(conn->messagedata);
	conn->message.cursize = 0;
	// LordHavoc: (inspired by ProQuake) use a short connect timeout to
	// reduce effectiveness of connection request floods
	conn->timeout = realtime + net_connecttimeout.value;
	LHNETADDRESS_ToString(&conn->peeraddress, conn->address, sizeof(conn->address), true);
	conn->next = netconn_list;
	netconn_list = conn;
	return conn;
}

void NetConn_Close(netconn_t *conn)
{
	netconn_t *c;
	// remove connection from list
	if (conn == netconn_list)
		netconn_list = conn->next;
	else
	{
		for (c = netconn_list;c;c = c->next)
		{
			if (c->next == conn)
			{
				c->next = conn->next;
				break;
			}
		}
		// not found in list, we'll avoid crashing here...
		if (!c)
			return;
	}
	// free connection
	Mem_Free(conn);
}

static int clientport = -1;
static int clientport2 = -1;
static int hostport = -1;
static void NetConn_UpdateServerStuff(void)
{
	if (cls.state != ca_dedicated)
	{
		if (clientport2 != cl_netport.integer)
		{
			clientport2 = cl_netport.integer;
			if (cls.state == ca_connected)
				Con_Print("Changing \"cl_port\" will not take effect until you reconnect.\n");
		}
		if (cls.state == ca_disconnected && clientport != clientport2)
		{
			clientport = clientport2;
			NetConn_CloseClientPorts();
		}
		if (cl_numsockets == 0)
			NetConn_OpenClientPorts();
	}

	if (hostport != sv_netport.integer)
	{
		hostport = sv_netport.integer;
		if (sv.active)
			Con_Print("Changing \"port\" will not take effect until \"map\" command is executed.\n");
	}
}

int NetConn_ReceivedMessage(netconn_t *conn, unsigned char *data, int length)
{
	unsigned int count;
	unsigned int flags;
	unsigned int sequence;
	int qlength;

	if (length >= 8)
	{
		qlength = (unsigned int)BigLong(((int *)data)[0]);
		flags = qlength & ~NETFLAG_LENGTH_MASK;
		qlength &= NETFLAG_LENGTH_MASK;
		// control packets were already handled
		if (!(flags & NETFLAG_CTL) && qlength == length)
		{
			sequence = BigLong(((int *)data)[1]);
			packetsReceived++;
			data += 8;
			length -= 8;
			if (flags & NETFLAG_UNRELIABLE)
			{
				if (sequence >= conn->unreliableReceiveSequence)
				{
					if (sequence > conn->unreliableReceiveSequence)
					{
						count = sequence - conn->unreliableReceiveSequence;
						droppedDatagrams += count;
						Con_DPrintf("Dropped %u datagram(s)\n", count);
					}
					conn->unreliableReceiveSequence = sequence + 1;
					conn->lastMessageTime = realtime;
					conn->timeout = realtime + net_messagetimeout.value;
					unreliableMessagesReceived++;
					if (length > 0)
					{
						SZ_Clear(&net_message);
						SZ_Write(&net_message, data, length);
						MSG_BeginReading();
						return 2;
					}
				}
				else
					Con_DPrint("Got a stale datagram\n");
				return 1;
			}
			else if (flags & NETFLAG_ACK)
			{
				if (sequence == (conn->sendSequence - 1))
				{
					if (sequence == conn->ackSequence)
					{
						conn->ackSequence++;
						if (conn->ackSequence != conn->sendSequence)
							Con_DPrint("ack sequencing error\n");
						conn->lastMessageTime = realtime;
						conn->timeout = realtime + net_messagetimeout.value;
						conn->sendMessageLength -= MAX_PACKETFRAGMENT;
						if (conn->sendMessageLength > 0)
						{
							memcpy(conn->sendMessage, conn->sendMessage+MAX_PACKETFRAGMENT, conn->sendMessageLength);
							conn->sendNext = true;
							NetConn_SendMessageNext(conn);
						}
						else
						{
							conn->sendMessageLength = 0;
							conn->canSend = true;
						}
					}
					else
						Con_DPrint("Duplicate ACK received\n");
				}
				else
					Con_DPrint("Stale ACK received\n");
				return 1;
			}
			else if (flags & NETFLAG_DATA)
			{
				unsigned int temppacket[2];
				temppacket[0] = BigLong(8 | NETFLAG_ACK);
				temppacket[1] = BigLong(sequence);
				NetConn_Write(conn->mysocket, (unsigned char *)temppacket, 8, &conn->peeraddress);
				if (sequence == conn->receiveSequence)
				{
					conn->lastMessageTime = realtime;
					conn->timeout = realtime + net_messagetimeout.value;
					conn->receiveSequence++;
					if( conn->receiveMessageLength + length <= (int)sizeof( conn->receiveMessage ) ) {
						memcpy(conn->receiveMessage + conn->receiveMessageLength, data, length);
						conn->receiveMessageLength += length;
					} else {
						Con_Printf( "Reliable message (seq: %i) too big for message buffer!\n"
									"Dropping the message!\n", sequence );
						conn->receiveMessageLength = 0;
						return 1;
					}
					if (flags & NETFLAG_EOM)
					{
						reliableMessagesReceived++;
						length = conn->receiveMessageLength;
						conn->receiveMessageLength = 0;
						if (length > 0)
						{
							SZ_Clear(&net_message);
							SZ_Write(&net_message, conn->receiveMessage, length);
							MSG_BeginReading();
							return 2;
						}
					}
				}
				else
					receivedDuplicateCount++;
				return 1;
			}
		}
	}
	return 0;
}

void NetConn_ConnectionEstablished(lhnetsocket_t *mysocket, lhnetaddress_t *peeraddress)
{
	cls.connect_trying = false;
	M_Update_Return_Reason("");
	// the connection request succeeded, stop current connection and set up a new connection
	CL_Disconnect();
	// if we're connecting to a remote server, shut down any local server
	if (LHNETADDRESS_GetAddressType(peeraddress) != LHNETADDRESSTYPE_LOOP && sv.active)
		Host_ShutdownServer ();
	// allocate a net connection to keep track of things
	cls.netcon = NetConn_Open(mysocket, peeraddress);
	Con_Printf("Connection accepted to %s\n", cls.netcon->address);
	key_dest = key_game;
	m_state = m_none;
	cls.demonum = -1;			// not in the demo loop now
	cls.state = ca_connected;
	cls.signon = 0;				// need all the signon messages before playing
}

int NetConn_IsLocalGame(void)
{
	if (cls.state == ca_connected && sv.active && cl.maxclients == 1)
		return true;
	return false;
}

int NetConn_ClientParsePacket(lhnetsocket_t *mysocket, unsigned char *data, int length, lhnetaddress_t *peeraddress)
{
	int ret, c, control;
	const char *s;
	char *string, addressstring2[128], cname[128], ipstring[32];
	char stringbuf[16384];

	if (length >= 5 && data[0] == 255 && data[1] == 255 && data[2] == 255 && data[3] == 255)
	{
		// received a command string - strip off the packaging and put it
		// into our string buffer with NULL termination
		data += 4;
		length -= 4;
		length = min(length, (int)sizeof(stringbuf) - 1);
		memcpy(stringbuf, data, length);
		stringbuf[length] = 0;
		string = stringbuf;

		if (developer.integer)
		{
			LHNETADDRESS_ToString(peeraddress, addressstring2, sizeof(addressstring2), true);
			Con_Printf("NetConn_ClientParsePacket: %s sent us a command:\n", addressstring2);
			Com_HexDumpToConsole(data, length);
		}

		if (length > 10 && !memcmp(string, "challenge ", 10) && cls.connect_trying)
		{
			char protocolnames[1400];
			Protocol_Names(protocolnames, sizeof(protocolnames));
			LHNETADDRESS_ToString(peeraddress, addressstring2, sizeof(addressstring2), true);
			Con_Printf("\"%s\" received, sending connect request back to %s\n", string, addressstring2);
			M_Update_Return_Reason("Got challenge response");
			NetConn_WriteString(mysocket, va("\377\377\377\377connect\\protocol\\darkplaces 3\\protocols\\%s\\challenge\\%s", protocolnames, string + 10), peeraddress);
			return true;
		}
		if (length == 6 && !memcmp(string, "accept", 6) && cls.connect_trying)
		{
			M_Update_Return_Reason("Accepted");
			NetConn_ConnectionEstablished(mysocket, peeraddress);
			return true;
		}
		if (length > 7 && !memcmp(string, "reject ", 7) && cls.connect_trying)
		{
			char rejectreason[32];
			cls.connect_trying = false;
			string += 7;
			length = max(length - 7, (int)sizeof(rejectreason) - 1);
			memcpy(rejectreason, string, length);
			rejectreason[length] = 0;
			M_Update_Return_Reason(rejectreason);
			return true;
		}
		if (length >= 13 && !memcmp(string, "infoResponse\x0A", 13))
		{
			serverlist_info_t *info;
			int n;
			double pingtime;

			string += 13;
			// serverlist only uses text addresses
			LHNETADDRESS_ToString(peeraddress, cname, sizeof(cname), true);
			// search the cache for this server and update it
			for( n = 0; n < serverlist_cachecount; n++ )
				if( !strcmp( cname, serverlist_cache[n].info.cname ) )
					break;
			if( n == serverlist_cachecount ) {
				// LAN search doesnt require an answer from the master server so we wont
				// know the ping nor will it be initialized already...

				// find a slot
				if( serverlist_cachecount == SERVERLIST_TOTALSIZE )
					return true;

				memset(&serverlist_cache[serverlist_cachecount], 0, sizeof(serverlist_cache[serverlist_cachecount]));
				// store the data the engine cares about (address and ping)
				strlcpy (serverlist_cache[serverlist_cachecount].info.cname, cname, sizeof (serverlist_cache[serverlist_cachecount].info.cname));
				serverlist_cache[serverlist_cachecount].info.ping = 100000;
				serverlist_cache[serverlist_cachecount].querytime = realtime;
				// if not in the slist menu we should print the server to console
				if (serverlist_consoleoutput) {
					Con_Printf("querying %s\n", ipstring);
				}

				++serverlist_cachecount;
			}

			info = &serverlist_cache[n].info;
			if ((s = SearchInfostring(string, "gamename"     )) != NULL) strlcpy(info->game, s, sizeof (info->game));else info->game[0] = 0;
			if ((s = SearchInfostring(string, "modname"      )) != NULL) strlcpy(info->mod , s, sizeof (info->mod ));else info->mod[0]  = 0;
			if ((s = SearchInfostring(string, "mapname"      )) != NULL) strlcpy(info->map , s, sizeof (info->map ));else info->map[0]  = 0;
			if ((s = SearchInfostring(string, "hostname"     )) != NULL) strlcpy(info->name, s, sizeof (info->name));else info->name[0] = 0;
			if ((s = SearchInfostring(string, "protocol"     )) != NULL) info->protocol = atoi(s);else info->protocol = -1;
			if ((s = SearchInfostring(string, "clients"      )) != NULL) info->numplayers = atoi(s);else info->numplayers = 0;
			if ((s = SearchInfostring(string, "sv_maxclients")) != NULL) info->maxplayers = atoi(s);else info->maxplayers  = 0;

			if (info->ping == 100000)
					serverreplycount++;

			pingtime = (int)((realtime - serverlist_cache[n].querytime) * 1000.0);
			pingtime = bound(0, pingtime, 9999);
			// update the ping
			info->ping = pingtime;

			// legacy/old stuff move it to the menu ASAP

			// build description strings for the things users care about
			dpsnprintf(serverlist_cache[n].line1, sizeof(serverlist_cache[n].line1), "%c%c%5d%c%c%c%3u/%3u %-65.65s", STRING_COLOR_TAG, pingtime >= 300 ? '1' : (pingtime >= 200 ? '3' : '7'), (int)pingtime, STRING_COLOR_TAG, STRING_COLOR_DEFAULT + '0', info->protocol != NET_PROTOCOL_VERSION ? '*' : ' ', info->numplayers, info->maxplayers, info->name);
			dpsnprintf(serverlist_cache[n].line2, sizeof(serverlist_cache[n].line2), "%-21.21s %-19.19s %-17.17s %-20.20s", info->cname, info->game, info->mod, info->map);
			if( serverlist_cache[n].query == SQS_QUERIED ) {
				ServerList_ViewList_Remove( &serverlist_cache[n] );
			}
			// if not in the slist menu we should print the server to console (if wanted)
			else if( serverlist_consoleoutput )
				Con_Printf("%s\n%s\n", serverlist_cache[n].line1, serverlist_cache[n].line2);
			// and finally, update the view set
			ServerList_ViewList_Insert( &serverlist_cache[n] );
			serverlist_cache[n].query = SQS_QUERIED;

			return true;
		}
		if (!strncmp(string, "getserversResponse\\", 19) && serverlist_cachecount < SERVERLIST_TOTALSIZE)
		{
			// Extract the IP addresses
			data += 18;
			length -= 18;
			masterreplycount++;
			if (serverlist_consoleoutput)
				Con_Print("received server list...\n");
			while (length >= 7 && data[0] == '\\' && (data[1] != 0xFF || data[2] != 0xFF || data[3] != 0xFF || data[4] != 0xFF) && data[5] * 256 + data[6] != 0)
			{
				int n;

				dpsnprintf (ipstring, sizeof (ipstring), "%u.%u.%u.%u:%u", data[1], data[2], data[3], data[4], (data[5] << 8) | data[6]);
				if (developer.integer)
					Con_Printf("Requesting info from server %s\n", ipstring);
				// ignore the rest of the message if the serverlist is full
				if( serverlist_cachecount == SERVERLIST_TOTALSIZE )
					break;
				// also ignore it if we have already queried it (other master server response)
				for( n = 0 ; n < serverlist_cachecount ; n++ )
					if( !strcmp( ipstring, serverlist_cache[ n ].info.cname ) )
						break;
				if( n >= serverlist_cachecount )
				{
					serverquerycount++;

					memset(&serverlist_cache[serverlist_cachecount], 0, sizeof(serverlist_cache[serverlist_cachecount]));
					// store the data the engine cares about (address and ping)
					strlcpy (serverlist_cache[serverlist_cachecount].info.cname, ipstring, sizeof (serverlist_cache[serverlist_cachecount].info.cname));
					serverlist_cache[serverlist_cachecount].info.ping = 100000;
					serverlist_cache[serverlist_cachecount].query = SQS_QUERYING;

					++serverlist_cachecount;
				}

				// move on to next address in packet
				data += 7;
				length -= 7;
			}
			// begin or resume serverlist queries
			serverlist_querysleep = false;
			return true;
		}
		/*
		if (!strncmp(string, "ping", 4))
		{
			if (developer.integer)
				Con_Printf("Received ping from %s, sending ack\n", UDP_AddrToString(readaddr));
			NetConn_WriteString(mysocket, "\377\377\377\377ack", peeraddress);
			return true;
		}
		if (!strncmp(string, "ack", 3))
			return true;
		*/
		// we may not have liked the packet, but it was a command packet, so
		// we're done processing this packet now
		return true;
	}
	// netquake control packets, supported for compatibility only
	if (length >= 5 && (control = BigLong(*((int *)data))) && (control & (~NETFLAG_LENGTH_MASK)) == (int)NETFLAG_CTL && (control & NETFLAG_LENGTH_MASK) == length)
	{
		c = data[4];
		data += 5;
		length -= 5;
		LHNETADDRESS_ToString(peeraddress, addressstring2, sizeof(addressstring2), true);
		switch (c)
		{
		case CCREP_ACCEPT:
			if (developer.integer)
				Con_Printf("Datagram_ParseConnectionless: received CCREP_ACCEPT from %s.\n", addressstring2);
			if (cls.connect_trying)
			{
				lhnetaddress_t clientportaddress;
				clientportaddress = *peeraddress;
				if (length >= 4)
				{
					unsigned int port = (data[0] << 0) | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
					data += 4;
					length -= 4;
					LHNETADDRESS_SetPort(&clientportaddress, port);
				}
				M_Update_Return_Reason("Accepted");
				NetConn_ConnectionEstablished(mysocket, &clientportaddress);
			}
			break;
		case CCREP_REJECT:
			if (developer.integer)
				Con_Printf("Datagram_ParseConnectionless: received CCREP_REJECT from %s.\n", addressstring2);
			cls.connect_trying = false;
			M_Update_Return_Reason((char *)data);
			break;
#if 0
		case CCREP_SERVER_INFO:
			if (developer.integer)
				Con_Printf("Datagram_ParseConnectionless: received CCREP_SERVER_INFO from %s.\n", addressstring2);
			if (cls.state != ca_dedicated)
			{
				// LordHavoc: because the UDP driver reports 0.0.0.0:26000 as the address
				// string we just ignore it and keep the real address
				MSG_ReadString();
				// serverlist only uses text addresses
				cname = UDP_AddrToString(readaddr);
				// search the cache for this server
				for (n = 0; n < hostCacheCount; n++)
					if (!strcmp(cname, serverlist[n].cname))
						break;
				// add it
				if (n == hostCacheCount && hostCacheCount < SERVERLISTSIZE)
				{
					hostCacheCount++;
					memset(&serverlist[n], 0, sizeof(serverlist[n]));
					strlcpy (serverlist[n].name, MSG_ReadString(), sizeof (serverlist[n].name));
					strlcpy (serverlist[n].map, MSG_ReadString(), sizeof (serverlist[n].map));
					serverlist[n].users = MSG_ReadByte();
					serverlist[n].maxusers = MSG_ReadByte();
					c = MSG_ReadByte();
					if (c != NET_PROTOCOL_VERSION)
					{
						strlcpy (serverlist[n].cname, serverlist[n].name, sizeof (serverlist[n].cname));
						strcpy(serverlist[n].name, "*");
						strlcat (serverlist[n].name, serverlist[n].cname, sizeof(serverlist[n].name));
					}
					strlcpy (serverlist[n].cname, cname, sizeof (serverlist[n].cname));
				}
			}
			break;
		case CCREP_PLAYER_INFO:
			// we got a CCREP_PLAYER_INFO??
			//if (developer.integer)
				Con_Printf("Datagram_ParseConnectionless: received CCREP_PLAYER_INFO from %s.\n", addressstring2);
			break;
		case CCREP_RULE_INFO:
			// we got a CCREP_RULE_INFO??
			//if (developer.integer)
				Con_Printf("Datagram_ParseConnectionless: received CCREP_RULE_INFO from %s.\n", addressstring2);
			break;
#endif
		default:
			break;
		}
		// we may not have liked the packet, but it was a valid control
		// packet, so we're done processing this packet now
		return true;
	}
	ret = 0;
	if (length >= (int)NET_HEADERSIZE && cls.netcon && mysocket == cls.netcon->mysocket && !LHNETADDRESS_Compare(&cls.netcon->peeraddress, peeraddress) && (ret = NetConn_ReceivedMessage(cls.netcon, data, length)) == 2)
		CL_ParseServerMessage();
	return ret;
}

void NetConn_QueryQueueFrame(void)
{
	int index;
	int queries;
	int maxqueries;
	double timeouttime;
	static double querycounter = 0;

	if (serverlist_querysleep)
		return;

	// each time querycounter reaches 1.0 issue a query
	querycounter += host_realframetime * net_slist_queriespersecond.value;
	maxqueries = (int)querycounter;
	maxqueries = bound(0, maxqueries, net_slist_queriesperframe.integer);
	querycounter -= maxqueries;

	if( maxqueries == 0 ) {
		return;
	}

	// scan serverlist and issue queries as needed
    serverlist_querysleep = true;

	timeouttime = realtime - net_slist_timeout.value;
	for( index = 0, queries = 0 ; index < serverlist_cachecount && queries < maxqueries ; index++ )
	{
		serverlist_entry_t *entry = &serverlist_cache[ index ];
		if( entry->query != SQS_QUERYING )
		{
			continue;
		}

        serverlist_querysleep = false;
		if( entry->querycounter != 0 && entry->querytime > timeouttime )
		{
			continue;
		}

		if( entry->querycounter != (unsigned) net_slist_maxtries.integer )
		{
			lhnetaddress_t address;
			int socket;

			LHNETADDRESS_FromString(&address, entry->info.cname, 0);
			for (socket = 0; socket < cl_numsockets ; socket++) {
				NetConn_WriteString(cl_sockets[socket], "\377\377\377\377getinfo", &address);
			}

			entry->querytime = realtime;
			entry->querycounter++;

			// if not in the slist menu we should print the server to console
			if (serverlist_consoleoutput)
				Con_Printf("querying %25s (%i. try)\n", entry->info.cname, entry->querycounter);

			queries++;
		}
		else
		{
			entry->query = SQS_TIMEDOUT;
		}
	}
}

void NetConn_ClientFrame(void)
{
	int i, length;
	lhnetaddress_t peeraddress;
	netconn_t *conn;
	NetConn_UpdateServerStuff();
	if (cls.connect_trying && cls.connect_nextsendtime < realtime)
	{
		if (cls.connect_remainingtries == 0)
			M_Update_Return_Reason("Connect: Waiting 10 seconds for reply");
		cls.connect_nextsendtime = realtime + 1;
		cls.connect_remainingtries--;
		if (cls.connect_remainingtries <= -10)
		{
			cls.connect_trying = false;
			M_Update_Return_Reason("Connect: Failed");
			return;
		}
		// try challenge first (newer server)
		NetConn_WriteString(cls.connect_mysocket, "\377\377\377\377getchallenge", &cls.connect_address);
		// then try netquake as a fallback (old server, or netquake)
		SZ_Clear(&net_message);
		// save space for the header, filled in later
		MSG_WriteLong(&net_message, 0);
		MSG_WriteByte(&net_message, CCREQ_CONNECT);
		MSG_WriteString(&net_message, "QUAKE");
		MSG_WriteByte(&net_message, NET_PROTOCOL_VERSION);
		*((int *)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
		NetConn_Write(cls.connect_mysocket, net_message.data, net_message.cursize, &cls.connect_address);
		SZ_Clear(&net_message);
	}
	for (i = 0;i < cl_numsockets;i++) {
		while (cl_sockets[i] && (length = NetConn_Read(cl_sockets[i], readbuffer, sizeof(readbuffer), &peeraddress)) > 0) {
			NetConn_ClientParsePacket(cl_sockets[i], readbuffer, length, &peeraddress);
		}
	}
	NetConn_QueryQueueFrame();
	if (cls.netcon && realtime > cls.netcon->timeout)
	{
		Con_Print("Connection timed out\n");
		CL_Disconnect();
		Host_ShutdownServer ();
	}
	for (conn = netconn_list;conn;conn = conn->next)
		NetConn_ReSendMessage(conn);
}

#define MAX_CHALLENGES 128
struct challenge_s
{
	lhnetaddress_t address;
	double time;
	char string[12];
}
challenge[MAX_CHALLENGES];

static void NetConn_BuildChallengeString(char *buffer, int bufferlength)
{
	int i;
	char c;
	for (i = 0;i < bufferlength - 1;i++)
	{
		do
		{
			c = rand () % (127 - 33) + 33;
		} while (c == '\\' || c == ';' || c == '"' || c == '%' || c == '/');
		buffer[i] = c;
	}
	buffer[i] = 0;
}

static qboolean NetConn_BuildStatusResponse(const char* challenge, char* out_msg, size_t out_size, qboolean fullstatus)
{
	unsigned int nb_clients = 0, i;
	int length;

	// How many clients are there?
	for (i = 0;i < (unsigned int)svs.maxclients;i++)
		if (svs.clients[i].active)
			nb_clients++;

	// TODO: we should add more information for the full status string
	length = dpsnprintf(out_msg, out_size,
						"\377\377\377\377%s\x0A"
						"\\gamename\\%s\\modname\\%s\\sv_maxclients\\%d"
						"\\clients\\%d\\mapname\\%s\\hostname\\%s""\\protocol\\%d"
						"%s%s"
						"%s",
						fullstatus ? "statusResponse" : "infoResponse",
						gamename, com_modname, svs.maxclients,
						nb_clients, sv.name, hostname.string, NET_PROTOCOL_VERSION,
						challenge ? "\\challenge\\" : "", challenge ? challenge : "",
						fullstatus ? "\n" : "");

	// Make sure it fits in the buffer
	if (length < 0)
		return false;

	if (fullstatus)
	{
		char *ptr;
		int left;

		ptr = out_msg + length;
		left = (int)out_size - length;

		for (i = 0;i < (unsigned int)svs.maxclients;i++)
		{
			client_t *cl = &svs.clients[i];
			if (cl->active)
			{
				int nameind, cleanind;
				char curchar;
				char cleanname [sizeof(cl->name)];

				// Remove all characters '"' and '\' in the player name
				nameind = 0;
				cleanind = 0;
				do
				{
					curchar = cl->name[nameind++];
					if (curchar != '"' && curchar != '\\')
					{
						cleanname[cleanind++] = curchar;
						if (cleanind == sizeof(cleanname) - 1)
							break;
					}
				} while (curchar != '\0');

				length = dpsnprintf(ptr, left, "%d %d \"%s\"\n",
									cl->frags,
									(int)(cl->ping * 1000.0f),
									cleanname);
				if(length < 0)
					return false;
				left -= length;
				ptr += length;
			}
		}
	}

	return true;
}

extern void SV_SendServerinfo (client_t *client);
int NetConn_ServerParsePacket(lhnetsocket_t *mysocket, unsigned char *data, int length, lhnetaddress_t *peeraddress)
{
	int i, ret, clientnum, best;
	double besttime;
	client_t *client;
	netconn_t *conn;
	char *s, *string, response[512], addressstring2[128], stringbuf[16384];

	if (sv.active)
	{
		if (length >= 5 && data[0] == 255 && data[1] == 255 && data[2] == 255 && data[3] == 255)
		{
			// received a command string - strip off the packaging and put it
			// into our string buffer with NULL termination
			data += 4;
			length -= 4;
			length = min(length, (int)sizeof(stringbuf) - 1);
			memcpy(stringbuf, data, length);
			stringbuf[length] = 0;
			string = stringbuf;

			if (developer.integer)
			{
				LHNETADDRESS_ToString(peeraddress, addressstring2, sizeof(addressstring2), true);
				Con_Printf("NetConn_ServerParsePacket: %s sent us a command:\n", addressstring2);
				Com_HexDumpToConsole(data, length);
			}

			if (length >= 12 && !memcmp(string, "getchallenge", 12))
			{
				for (i = 0, best = 0, besttime = realtime;i < MAX_CHALLENGES;i++)
				{
					if (!LHNETADDRESS_Compare(peeraddress, &challenge[i].address))
						break;
					if (besttime > challenge[i].time)
						besttime = challenge[best = i].time;
				}
				// if we did not find an exact match, choose the oldest and
				// update address and string
				if (i == MAX_CHALLENGES)
				{
					i = best;
					challenge[i].address = *peeraddress;
					NetConn_BuildChallengeString(challenge[i].string, sizeof(challenge[i].string));
				}
				challenge[i].time = realtime;
				// send the challenge
				NetConn_WriteString(mysocket, va("\377\377\377\377challenge %s", challenge[i].string), peeraddress);
				return true;
			}
			if (length > 8 && !memcmp(string, "connect\\", 8))
			{
				string += 7;
				length -= 7;
				if ((s = SearchInfostring(string, "challenge")))
				{
					// validate the challenge
					for (i = 0;i < MAX_CHALLENGES;i++)
						if (!LHNETADDRESS_Compare(peeraddress, &challenge[i].address) && !strcmp(challenge[i].string, s))
							break;
					if (i < MAX_CHALLENGES)
					{
						// check engine protocol
						if (strcmp(SearchInfostring(string, "protocol"), "darkplaces 3"))
						{
							if (developer.integer)
								Con_Printf("Datagram_ParseConnectionless: sending \"reject Wrong game protocol.\" to %s.\n", addressstring2);
							NetConn_WriteString(mysocket, "\377\377\377\377reject Wrong game protocol.", peeraddress);
						}
						else
						{
							// see if this is a duplicate connection request
							for (clientnum = 0, client = svs.clients;clientnum < svs.maxclients;clientnum++, client++)
								if (client->netconnection && LHNETADDRESS_Compare(peeraddress, &client->netconnection->peeraddress) == 0)
									break;
							if (clientnum < svs.maxclients && realtime - client->connecttime < net_messagerejointimeout.value)
							{
								// client is still trying to connect,
								// so we send a duplicate reply
								if (developer.integer)
									Con_Printf("Datagram_ParseConnectionless: sending duplicate accept to %s.\n", addressstring2);
								NetConn_WriteString(mysocket, "\377\377\377\377accept", peeraddress);
							}
#if 0
							else if (clientnum < svs.maxclients)
							{
								if (realtime - client->netconnection->lastMessageTime >= net_messagerejointimeout.value)
								{
									// client crashed and is coming back, keep their stuff intact
									SV_SendServerinfo(client);
									//host_client = client;
									//SV_DropClient (true);
								}
								// else ignore them
							}
#endif
							else
							{
								// this is a new client, find a slot
								for (clientnum = 0, client = svs.clients;clientnum < svs.maxclients;clientnum++, client++)
									if (!client->active)
										break;
								if (clientnum < svs.maxclients)
								{
									// prepare the client struct
									if ((conn = NetConn_Open(mysocket, peeraddress)))
									{
										// allocated connection
										LHNETADDRESS_ToString(peeraddress, conn->address, sizeof(conn->address), true);
										if (developer.integer)
											Con_Printf("Datagram_ParseConnectionless: sending \"accept\" to %s.\n", conn->address);
										NetConn_WriteString(mysocket, "\377\377\377\377accept", peeraddress);
										// now set up the client
										SV_ConnectClient(clientnum, conn);
										NetConn_Heartbeat(1);
									}
								}
								else
								{
									// server is full
									if (developer.integer)
										Con_Printf("Datagram_ParseConnectionless: sending \"reject Server is full.\" to %s.\n", addressstring2);
									NetConn_WriteString(mysocket, "\377\377\377\377reject Server is full.", peeraddress);
								}
							}
						}
					}
				}
				return true;
			}
			if (length >= 7 && !memcmp(string, "getinfo", 7))
			{
				const char *challenge = NULL;

				// If there was a challenge in the getinfo message
				if (length > 8 && string[7] == ' ')
					challenge = string + 8;

				if (NetConn_BuildStatusResponse(challenge, response, sizeof(response), false))
				{
					if (developer.integer)
						Con_Printf("Sending reply to master %s - %s\n", addressstring2, response);
					NetConn_WriteString(mysocket, response, peeraddress);
				}
				return true;
			}
			if (length >= 9 && !memcmp(string, "getstatus", 9))
			{
				const char *challenge = NULL;

				// If there was a challenge in the getinfo message
				if (length > 10 && string[9] == ' ')
					challenge = string + 10;

				if (NetConn_BuildStatusResponse(challenge, response, sizeof(response), true))
				{
					if (developer.integer)
						Con_Printf("Sending reply to client %s - %s\n", addressstring2, response);
					NetConn_WriteString(mysocket, response, peeraddress);
				}
				return true;
			}
			/*
			if (!strncmp(string, "ping", 4))
			{
				if (developer.integer)
					Con_Printf("Received ping from %s, sending ack\n", UDP_AddrToString(readaddr));
				NetConn_WriteString(mysocket, "\377\377\377\377ack", peeraddress);
				return true;
			}
			if (!strncmp(string, "ack", 3))
				return true;
			*/
			// we may not have liked the packet, but it was a command packet, so
			// we're done processing this packet now
			return true;
		}
		// LordHavoc: disabled netquake control packet support in server
#if 0
		{
			int c, control;
			// netquake control packets, supported for compatibility only
			if (length >= 5 && (control = BigLong(*((int *)data))) && (control & (~NETFLAG_LENGTH_MASK)) == (int)NETFLAG_CTL && (control & NETFLAG_LENGTH_MASK) == length)
			{
				c = data[4];
				data += 5;
				length -= 5;
				LHNETADDRESS_ToString(peeraddress, addressstring2, sizeof(addressstring2), true);
				switch (c)
				{
				case CCREQ_CONNECT:
					//if (developer.integer)
						Con_Printf("Datagram_ParseConnectionless: received CCREQ_CONNECT from %s.\n", addressstring2);
					if (length >= (int)strlen("QUAKE") + 1 + 1)
					{
						if (memcmp(data, "QUAKE", strlen("QUAKE") + 1) != 0 || (int)data[strlen("QUAKE") + 1] != NET_PROTOCOL_VERSION)
						{
							if (developer.integer)
								Con_Printf("Datagram_ParseConnectionless: sending CCREP_REJECT \"Incompatible version.\" to %s.\n", addressstring2);
							SZ_Clear(&net_message);
							// save space for the header, filled in later
							MSG_WriteLong(&net_message, 0);
							MSG_WriteByte(&net_message, CCREP_REJECT);
							MSG_WriteString(&net_message, "Incompatible version.\n");
							*((int *)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
							NetConn_Write(mysocket, net_message.data, net_message.cursize, peeraddress);
							SZ_Clear(&net_message);
						}
						else
						{
							// see if this is a duplicate connection request
							for (clientnum = 0, client = svs.clients;clientnum < svs.maxclients;clientnum++, client++)
								if (client->netconnection && LHNETADDRESS_Compare(peeraddress, &client->netconnection->peeraddress) == 0)
									break;
							if (clientnum < svs.maxclients)
							{
								// duplicate connection request
								if (realtime - client->connecttime < 2.0)
								{
									// client is still trying to connect,
									// so we send a duplicate reply
									if (developer.integer)
										Con_Printf("Datagram_ParseConnectionless: sending duplicate CCREP_ACCEPT to %s.\n", addressstring2);
									SZ_Clear(&net_message);
									// save space for the header, filled in later
									MSG_WriteLong(&net_message, 0);
									MSG_WriteByte(&net_message, CCREP_ACCEPT);
									MSG_WriteLong(&net_message, LHNETADDRESS_GetPort(LHNET_AddressFromSocket(client->netconnection->mysocket)));
									*((int *)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
									NetConn_Write(mysocket, net_message.data, net_message.cursize, peeraddress);
									SZ_Clear(&net_message);
								}
#if 0
								else if (realtime - client->netconnection->lastMessageTime >= net_messagerejointimeout.value)
								{
									SV_SendServerinfo(client);
									// the old client hasn't sent us anything
									// in quite a while, so kick off and let
									// the retry take care of it...
									//host_client = client;
									//SV_DropClient (true);
								}
#endif
							}
							else
							{
								// this is a new client, find a slot
								for (clientnum = 0, client = svs.clients;clientnum < svs.maxclients;clientnum++, client++)
									if (!client->active)
										break;
								if (clientnum < svs.maxclients && (client->netconnection = conn = NetConn_Open(mysocket, peeraddress)) != NULL)
								{
									// connect to the client
									// everything is allocated, just fill in the details
									strlcpy (conn->address, addressstring2, sizeof (conn->address));
									if (developer.integer)
										Con_Printf("Datagram_ParseConnectionless: sending CCREP_ACCEPT to %s.\n", addressstring2);
									// send back the info about the server connection
									SZ_Clear(&net_message);
									// save space for the header, filled in later
									MSG_WriteLong(&net_message, 0);
									MSG_WriteByte(&net_message, CCREP_ACCEPT);
									MSG_WriteLong(&net_message, LHNETADDRESS_GetPort(LHNET_AddressFromSocket(conn->mysocket)));
									*((int *)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
									NetConn_Write(mysocket, net_message.data, net_message.cursize, peeraddress);
									SZ_Clear(&net_message);
									// now set up the client struct
									SV_ConnectClient(clientnum, conn);
									NetConn_Heartbeat(1);
								}
								else
								{
									//if (developer.integer)
										Con_Printf("Datagram_ParseConnectionless: sending CCREP_REJECT \"Server is full.\" to %s.\n", addressstring2);
									// no room; try to let player know
									SZ_Clear(&net_message);
									// save space for the header, filled in later
									MSG_WriteLong(&net_message, 0);
									MSG_WriteByte(&net_message, CCREP_REJECT);
									MSG_WriteString(&net_message, "Server is full.\n");
									*((int *)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
									NetConn_Write(mysocket, net_message.data, net_message.cursize, peeraddress);
									SZ_Clear(&net_message);
								}
							}
						}
					}
					break;
#if 0
				case CCREQ_SERVER_INFO:
					if (developer.integer)
						Con_Printf("Datagram_ParseConnectionless: received CCREQ_SERVER_INFO from %s.\n", addressstring2);
					if (sv.active && !strcmp(MSG_ReadString(), "QUAKE"))
					{
						if (developer.integer)
							Con_Printf("Datagram_ParseConnectionless: sending CCREP_SERVER_INFO to %s.\n", addressstring2);
						SZ_Clear(&net_message);
						// save space for the header, filled in later
						MSG_WriteLong(&net_message, 0);
						MSG_WriteByte(&net_message, CCREP_SERVER_INFO);
						UDP_GetSocketAddr(UDP_acceptSock, &newaddr);
						MSG_WriteString(&net_message, UDP_AddrToString(&newaddr));
						MSG_WriteString(&net_message, hostname.string);
						MSG_WriteString(&net_message, sv.name);
						MSG_WriteByte(&net_message, net_activeconnections);
						MSG_WriteByte(&net_message, svs.maxclients);
						MSG_WriteByte(&net_message, NET_PROTOCOL_VERSION);
						*((int *)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
						NetConn_Write(mysocket, net_message.data, net_message.cursize, peeraddress);
						SZ_Clear(&net_message);
					}
					break;
				case CCREQ_PLAYER_INFO:
					if (developer.integer)
						Con_Printf("Datagram_ParseConnectionless: received CCREQ_PLAYER_INFO from %s.\n", addressstring2);
					if (sv.active)
					{
						int playerNumber, activeNumber, clientNumber;
						client_t *client;

						playerNumber = MSG_ReadByte();
						activeNumber = -1;
						for (clientNumber = 0, client = svs.clients; clientNumber < svs.maxclients; clientNumber++, client++)
							if (client->active && ++activeNumber == playerNumber)
								break;
						if (clientNumber != svs.maxclients)
						{
							SZ_Clear(&net_message);
							// save space for the header, filled in later
							MSG_WriteLong(&net_message, 0);
							MSG_WriteByte(&net_message, CCREP_PLAYER_INFO);
							MSG_WriteByte(&net_message, playerNumber);
							MSG_WriteString(&net_message, client->name);
							MSG_WriteLong(&net_message, client->colors);
							MSG_WriteLong(&net_message, (int)client->edict->fields.server->frags);
							MSG_WriteLong(&net_message, (int)(realtime - client->connecttime));
							MSG_WriteString(&net_message, client->netconnection ? client->netconnection->address : "botclient");
							*((int *)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
							NetConn_Write(mysocket, net_message.data, net_message.cursize, peeraddress);
							SZ_Clear(&net_message);
						}
					}
					break;
				case CCREQ_RULE_INFO:
					if (developer.integer)
						Con_Printf("Datagram_ParseConnectionless: received CCREQ_RULE_INFO from %s.\n", addressstring2);
					if (sv.active)
					{
						char *prevCvarName;
						cvar_t *var;

						// find the search start location
						prevCvarName = MSG_ReadString();
						var = Cvar_FindVarAfter(prevCvarName, CVAR_NOTIFY);

						// send the response
						SZ_Clear(&net_message);
						// save space for the header, filled in later
						MSG_WriteLong(&net_message, 0);
						MSG_WriteByte(&net_message, CCREP_RULE_INFO);
						if (var)
						{
							MSG_WriteString(&net_message, var->name);
							MSG_WriteString(&net_message, var->string);
						}
						*((int *)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
						NetConn_Write(mysocket, net_message.data, net_message.cursize, peeraddress);
						SZ_Clear(&net_message);
					}
					break;
#endif
				default:
					break;
				}
				// we may not have liked the packet, but it was a valid control
				// packet, so we're done processing this packet now
				return true;
			}
		}
#endif
		for (i = 0, host_client = svs.clients;i < svs.maxclients;i++, host_client++)
		{
			if (host_client->netconnection && host_client->netconnection->mysocket == mysocket && !LHNETADDRESS_Compare(&host_client->netconnection->peeraddress, peeraddress))
			{
				if ((ret = NetConn_ReceivedMessage(host_client->netconnection, data, length)) == 2)
					SV_ReadClientMessage();
				return ret;
			}
		}
	}
	return 0;
}

void NetConn_ServerFrame(void)
{
	int i, length;
	lhnetaddress_t peeraddress;
	netconn_t *conn;
	NetConn_UpdateServerStuff();
	for (i = 0;i < sv_numsockets;i++)
		while (sv_sockets[i] && (length = NetConn_Read(sv_sockets[i], readbuffer, sizeof(readbuffer), &peeraddress)) > 0)
			NetConn_ServerParsePacket(sv_sockets[i], readbuffer, length, &peeraddress);
	for (i = 0, host_client = svs.clients;i < svs.maxclients;i++, host_client++)
	{
		// never timeout loopback connections
		if (host_client->netconnection && realtime > host_client->netconnection->timeout && LHNETADDRESS_GetAddressType(&host_client->netconnection->peeraddress) != LHNETADDRESSTYPE_LOOP)
		{
			Con_Printf("Client \"%s\" connection timed out\n", host_client->name);
			SV_DropClient(false);
		}
	}
	for (conn = netconn_list;conn;conn = conn->next)
		NetConn_ReSendMessage(conn);
}

void NetConn_QueryMasters(void)
{
	int i;
	int masternum;
	lhnetaddress_t masteraddress;
	lhnetaddress_t broadcastaddress;
	char request[256];

	if (serverlist_cachecount >= SERVERLIST_TOTALSIZE)
		return;

	// 26000 is the default quake server port, servers on other ports will not
	// be found
	// note this is IPv4-only, I doubt there are IPv6-only LANs out there
	LHNETADDRESS_FromString(&broadcastaddress, "255.255.255.255", 26000);

	for (i = 0;i < cl_numsockets;i++)
	{
		if (cl_sockets[i])
		{
			// search LAN for Quake servers
			SZ_Clear(&net_message);
			// save space for the header, filled in later
			MSG_WriteLong(&net_message, 0);
			MSG_WriteByte(&net_message, CCREQ_SERVER_INFO);
			MSG_WriteString(&net_message, "QUAKE");
			MSG_WriteByte(&net_message, NET_PROTOCOL_VERSION);
			*((int *)net_message.data) = BigLong(NETFLAG_CTL | (net_message.cursize & NETFLAG_LENGTH_MASK));
			NetConn_Write(cl_sockets[i], net_message.data, net_message.cursize, &broadcastaddress);
			SZ_Clear(&net_message);

			// search LAN for DarkPlaces servers
			NetConn_WriteString(cl_sockets[i], "\377\377\377\377getinfo", &broadcastaddress);

			// build the getservers message to send to the master servers
			dpsnprintf(request, sizeof(request), "\377\377\377\377getservers %s %u empty full\x0A", gamename, NET_PROTOCOL_VERSION);

			// search internet
			for (masternum = 0;sv_masters[masternum].name;masternum++)
			{
				if (sv_masters[masternum].string && LHNETADDRESS_FromString(&masteraddress, sv_masters[masternum].string, MASTER_PORT) && LHNETADDRESS_GetAddressType(&masteraddress) == LHNETADDRESS_GetAddressType(LHNET_AddressFromSocket(cl_sockets[i])))
				{
					masterquerycount++;
					NetConn_WriteString(cl_sockets[i], request, &masteraddress);
				}
			}
		}
	}
	if (!masterquerycount)
	{
		Con_Print("Unable to query master servers, no suitable network sockets active.\n");
		M_Update_Return_Reason("No network");
	}
}

void NetConn_Heartbeat(int priority)
{
	lhnetaddress_t masteraddress;
	int masternum;
	lhnetsocket_t *mysocket;

	// if it's a state change (client connected), limit next heartbeat to no
	// more than 30 sec in the future
	if (priority == 1 && nextheartbeattime > realtime + 30.0)
		nextheartbeattime = realtime + 30.0;

	// limit heartbeatperiod to 30 to 270 second range,
	// lower limit is to avoid abusing master servers with excess traffic,
	// upper limit is to avoid timing out on the master server (which uses
	// 300 sec timeout)
	if (sv_heartbeatperiod.value < 30)
		Cvar_SetValueQuick(&sv_heartbeatperiod, 30);
	if (sv_heartbeatperiod.value > 270)
		Cvar_SetValueQuick(&sv_heartbeatperiod, 270);

	// make advertising optional and don't advertise singleplayer games, and
	// only send a heartbeat as often as the admin wants
	if (sv.active && sv_public.integer && svs.maxclients >= 2 && (priority > 1 || realtime > nextheartbeattime))
	{
		nextheartbeattime = realtime + sv_heartbeatperiod.value;
		for (masternum = 0;sv_masters[masternum].name;masternum++)
			if (sv_masters[masternum].string && LHNETADDRESS_FromString(&masteraddress, sv_masters[masternum].string, MASTER_PORT) && (mysocket = NetConn_ChooseServerSocketForAddress(&masteraddress)))
				NetConn_WriteString(mysocket, "\377\377\377\377heartbeat DarkPlaces\x0A", &masteraddress);
	}
}

static void Net_Heartbeat_f(void)
{
	if (sv.active)
		NetConn_Heartbeat(2);
	else
		Con_Print("No server running, can not heartbeat to master server.\n");
}

void PrintStats(netconn_t *conn)
{
	Con_Printf("address=%21s canSend=%u sendSeq=%6u recvSeq=%6u\n", conn->address, conn->canSend, conn->sendSequence, conn->receiveSequence);
}

void Net_Stats_f(void)
{
	netconn_t *conn;
	Con_Printf("unreliable messages sent   = %i\n", unreliableMessagesSent);
	Con_Printf("unreliable messages recv   = %i\n", unreliableMessagesReceived);
	Con_Printf("reliable messages sent     = %i\n", reliableMessagesSent);
	Con_Printf("reliable messages received = %i\n", reliableMessagesReceived);
	Con_Printf("packetsSent                = %i\n", packetsSent);
	Con_Printf("packetsReSent              = %i\n", packetsReSent);
	Con_Printf("packetsReceived            = %i\n", packetsReceived);
	Con_Printf("receivedDuplicateCount     = %i\n", receivedDuplicateCount);
	Con_Printf("droppedDatagrams           = %i\n", droppedDatagrams);
	Con_Print("connections                =\n");
	for (conn = netconn_list;conn;conn = conn->next)
		PrintStats(conn);
}

void Net_Slist_f(void)
{
	ServerList_ResetMasks();
	serverlist_sortbyfield = SLIF_PING;
	serverlist_sortdescending = false;
    if (m_state != m_slist) {
		Con_Print("Sending requests to master servers\n");
		ServerList_QueryList();
		serverlist_consoleoutput = true;
		Con_Print("Listening for replies...\n");
	} else
		ServerList_QueryList();
}

void NetConn_Init(void)
{
	int i;
	lhnetaddress_t tempaddress;
	netconn_mempool = Mem_AllocPool("network connections", 0, NULL);
	Cmd_AddCommand("net_stats", Net_Stats_f, "print network statistics");
	Cmd_AddCommand("net_slist", Net_Slist_f, "query master series and print all server information");
	Cmd_AddCommand("heartbeat", Net_Heartbeat_f, "send a heartbeat to the master server (updates your server information)");
	Cvar_RegisterVariable(&net_slist_queriespersecond);
	Cvar_RegisterVariable(&net_slist_queriesperframe);
	Cvar_RegisterVariable(&net_slist_timeout);
	Cvar_RegisterVariable(&net_slist_maxtries);
	Cvar_RegisterVariable(&net_messagetimeout);
	Cvar_RegisterVariable(&net_messagerejointimeout);
	Cvar_RegisterVariable(&net_connecttimeout);
	Cvar_RegisterVariable(&cl_netlocalping);
	Cvar_RegisterVariable(&cl_netpacketloss);
	Cvar_RegisterVariable(&hostname);
	Cvar_RegisterVariable(&developer_networking);
	Cvar_RegisterVariable(&cl_netport);
	Cvar_RegisterVariable(&sv_netport);
	Cvar_RegisterVariable(&net_address);
	//Cvar_RegisterVariable(&net_address_ipv6);
	Cvar_RegisterVariable(&sv_public);
	Cvar_RegisterVariable(&sv_heartbeatperiod);
	for (i = 0;sv_masters[i].name;i++)
		Cvar_RegisterVariable(&sv_masters[i]);
// COMMANDLINEOPTION: Server: -ip <ipaddress> sets the ip address of this machine for purposes of networking (default 0.0.0.0 also known as INADDR_ANY), use only if you have multiple network adapters and need to choose one specifically.
	if ((i = COM_CheckParm("-ip")) && i + 1 < com_argc)
	{
		if (LHNETADDRESS_FromString(&tempaddress, com_argv[i + 1], 0) == 1)
		{
			Con_Printf("-ip option used, setting net_address to \"%s\"\n");
			Cvar_SetQuick(&net_address, com_argv[i + 1]);
		}
		else
			Con_Printf("-ip option used, but unable to parse the address \"%s\"\n", com_argv[i + 1]);
	}
// COMMANDLINEOPTION: Server: -port <portnumber> sets the port to use for a server (default 26000, the same port as QUAKE itself), useful if you host multiple servers on your machine
	if (((i = COM_CheckParm("-port")) || (i = COM_CheckParm("-ipport")) || (i = COM_CheckParm("-udpport"))) && i + 1 < com_argc)
	{
		i = atoi(com_argv[i + 1]);
		if (i >= 0 && i < 65536)
		{
			Con_Printf("-port option used, setting port cvar to %i\n", i);
			Cvar_SetValueQuick(&sv_netport, i);
		}
		else
			Con_Printf("-port option used, but %i is not a valid port number\n", i);
	}
	cl_numsockets = 0;
	sv_numsockets = 0;
	net_message.data = net_message_buf;
	net_message.maxsize = sizeof(net_message_buf);
	net_message.cursize = 0;
	LHNET_Init();
}

void NetConn_Shutdown(void)
{
	NetConn_CloseClientPorts();
	NetConn_CloseServerPorts();
	LHNET_Shutdown();
}

