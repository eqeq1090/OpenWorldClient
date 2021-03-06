// Fill out your copyright notice in the Description page of Project Settings.


#include "GameTcpSocketConnection.h"
#include "Kismet/GameplayStatics.h"
#include "OpenWorldClient/MainControllerPC.h"
#include "SocketSubsystem.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "IPAddress.h"
#include "Sockets.h"
#include "HAL/RunnableThread.h"
#include "Async/Async.h"
#include <string>
#include "Logging/MessageLog.h"
#include "HAL/UnrealMemory.h"
#include "TcpSocketSettings.h"
#include <Runtime/Core/Public/Serialization/BufferArchive.h>
#include "OpenWorldClient/OpenWorldClientGameMode.h"
#include <Runtime/JsonUtilities/Public/JsonObjectConverter.h>

void AGameTcpSocketConnection::ConnectToGameServer()
{
	ConnectionIdGameServer = 0;
	if (IsConnected(ConnectionIdGameServer))
	{
		UE_LOG(LogTemp, Log, TEXT("Log: Can't connect Second time. We're already connected!"));
		return;
	}
	
	DisconnectedDelegate.BindUObject(this, &AGameTcpSocketConnection::OnDisconnected);
	ConnectedDelegate.BindUObject(this, &AGameTcpSocketConnection::OnConnected);
	MessageReceivedDelegate.BindUObject(this, &AGameTcpSocketConnection::OnMessageReceived);

	Connect("222.107.145.1", 9000, ConnectionIdGameServer);
}

void AGameTcpSocketConnection::OnConnected(int32 conID)
{
	UE_LOG(LogTemp, Log, TEXT("Log: Connected to server"));
}

void AGameTcpSocketConnection::OnDisconnected(int32 conID)
{
	UE_LOG(LogTemp, Log, TEXT("Log: Disconnected"));
}

/// <summary>
/// 서버에서 받은 메시지를 처리하는 부분
/// </summary>
/// <param name="ConId"></param>
/// <param name="Message"></param>
void AGameTcpSocketConnection::OnMessageReceived(int32 conID, TArray<uint8>& message)
{
	UE_LOG(LogTemp, Log, TEXT("Log: Received message"));

	while (message.Num() != 0)
	{
		int32 header = PopInt(message);
		if (header == -1)
			return;
		TArray<uint8> body;
		if (!PopBytes(header, message, body))
		{
			continue;
		}
		EProtocolType type;
		type = (EProtocolType)PopInt16(body);
		FString stringData;
		switch (type)
		{
		case EProtocolType::ChatMsgAck:
		{
			int16 msgLength = PopInt16(body);

			stringData = PopString(body, msgLength);

			FPacketChatMessageArrived ack;

			FJsonObjectConverter::JsonObjectStringToUStruct(stringData, &ack, 0, 0);

			ChatMessageDelegate.ExecuteIfBound(ack);
		}
			break;
		case EProtocolType::ConnectAck:
		{
			

			int16 msgLength = PopInt16(body);

			stringData = PopString(body, msgLength);
			FPacketConnectAck ack;
			//DeserializeJsonToStruct(&ack, stringData);
			FJsonObjectConverter::JsonObjectStringToUStruct(stringData, &ack, 0, 0);

			

			if (ack.ResultType == 1)
			{
				//닉네임 처리
				SetNicknameAckDelegate.ExecuteIfBound(ack.MyName);
			}

			// TODO: 필드정보를 통해 핃드 셋팅(유저 데이터)


			UE_LOG(LogTemp, Log, TEXT("Log: Received message"));
		}
			break;
		}   
	}
}
/// <summary>
/// 채팅 메시지 보내기
/// 패킷 형식에 맞춰서 버퍼 세팅
/// </summary>
/// <param name="message"></param>
void AGameTcpSocketConnection::SendMessage(const FString& message)
{
	UE_LOG(LogTemp, Log, TEXT("Log: SendMessage"));

	TArray<uint8> buffer;
	TArray<uint8> temp;
	//프로토콜 타입
	int16 type = (int16)EProtocolType::ChatMsgReq;
	TArray<uint8> typeToBytes = ConvInt16ToBytes(type);
	temp.Append(typeToBytes);
	//문자열 길이
	int16 msgLength = (int16)message.Len();
	TArray<uint8> msgLengthToBytes = ConvInt16ToBytes(msgLength);
	temp.Append(msgLengthToBytes);
	//문자열 
	TArray<uint8> msgToBytes = ConvStringToBytes(message);
	temp.Append(msgToBytes);
	//전체 버퍼 길이
	int32 header = temp.Num() + 4;
	TArray<uint8> headerToBytes = ConvIntToBytes(header);
	buffer.Append(headerToBytes);
	buffer.Append(temp);
	SendData(0, buffer);
}

void AGameTcpSocketConnection::SendMove(const int16 moveState, const FVector& pos, const FRotator& rot)
{
	UE_LOG(LogTemp, Log, TEXT("Log: SendMove"));

	FPacketPlayerMoveReq packet;

	packet.MoveState = (int16)moveState;
	packet.X = pos.X;
	packet.Y = pos.Y;
	packet.Z = pos.Z;

	packet.Pitch = rot.Pitch;
	packet.Yaw = rot.Yaw;
	packet.Roll = rot.Roll;
	FBufferArchive archive(true);
	FPacketPlayerMoveReq::StaticStruct()->SerializeBin(archive, &packet);

	TArray<uint8> data = archive;
	TArray<uint8> buffer;
	TArray<uint8> temp;
	//프로토콜 타입
	int16 type = (int16)EProtocolType::PlayerMoveAck;
	TArray<uint8> typeToBytes = ConvInt16ToBytes(type);
	temp.Append(typeToBytes);

	temp.Append(data);
	//전체 버퍼 길이
	int32 header = temp.Num() + 4;
	TArray<uint8> headerToBytes = ConvIntToBytes(header);
	buffer.Append(headerToBytes);
	buffer.Append(temp);
	SendData(0, buffer);
}

void AGameTcpSocketConnection::SendNickname(const FString& message)
{
	UE_LOG(LogTemp, Log, TEXT("Log: SendNickname"));

	TArray<uint8> buffer;
	TArray<uint8> temp;

	//프로토콜 타입
	int16 type = (int16)EProtocolType::ConnectReq;
	TArray<uint8> typeToBytes = ConvInt16ToBytes(type);
	temp.Append(typeToBytes);

	FPacketSetNicknameReq packet;

	packet.UserName = message;

	//구조체 json 직렬화(성능상 단점이 있지만 간편, FString 길이에 대해 일일히 지정할 필요가 없음)
	//자주 송수신 되지 않는 패킷에 사용
	FString json;
	FJsonObjectConverter::UStructToJsonObjectString(packet, json, 0, 0);


	//문자열 길이
	int16 msgLength = (int16)json.Len();
	TArray<uint8> msgLengthToBytes = ConvInt16ToBytes(msgLength);
	temp.Append(msgLengthToBytes);

	//json 문자열
	TArray<uint8> jsonToBytes = ConvStringToBytes(json);
	temp.Append(jsonToBytes);

	//전체 버퍼 길이
	int32 header = temp.Num() + 4;
	TArray<uint8> headerToBytes = ConvIntToBytes(header);
	buffer.Append(headerToBytes);
	buffer.Append(temp);
	SendData(0, buffer);
}

