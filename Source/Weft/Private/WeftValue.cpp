#include "WeftValue.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/Base64.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace WeftValue
{
	namespace
	{
		// MessagePack's subset weft writes (docs/engine-communication.md 6.1): nil, booleans, integers, floats, UTF-8
		// strings, bytes, arrays and maps of string keys; read big-endian, nested at most MaxDepth.
		constexpr int32 MaxDepth = 64;

		struct FReader
		{
			const uint8* At;
			const uint8* End;
			bool bBad = false;

			bool Need(SIZE_T Bytes)
			{
				if (SIZE_T(End - At) < Bytes)
				{
					bBad = true;
				}
				return !bBad;
			}

			uint64 Unsigned(int32 Bytes)
			{
				uint64 Value = 0;
				if (Need(Bytes))
				{
					for (int32 I = 0; I < Bytes; ++I)
					{
						Value = (Value << 8) | *At++;
					}
				}
				return Value;
			}

			FString Text(SIZE_T Bytes)
			{
				if (!Need(Bytes))
				{
					return FString();
				}
				const FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(At), int32(Bytes));
				At += Bytes;
				return FString(Converted.Length(), Converted.Get());
			}

			TSharedPtr<FJsonValue> Value(int32 Depth)
			{
				if (Depth > MaxDepth || !Need(1))
				{
					bBad = true;
					return MakeShared<FJsonValueNull>();
				}
				const uint8 Tag = *At++;
				const auto Array = [&](uint64 Count) -> TSharedPtr<FJsonValue>
				{
					TArray<TSharedPtr<FJsonValue>> Items;
					for (uint64 I = 0; I < Count && !bBad; ++I)
					{
						Items.Add(Value(Depth + 1));
					}
					return MakeShared<FJsonValueArray>(Items);
				};
				const auto Map = [&](uint64 Count) -> TSharedPtr<FJsonValue>
				{
					const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
					for (uint64 I = 0; I < Count && !bBad; ++I)
					{
						const TSharedPtr<FJsonValue> Key = Value(Depth + 1);
						const TSharedPtr<FJsonValue> Item = Value(Depth + 1);
						Object->SetField(Key->AsString(), Item);
					}
					return MakeShared<FJsonValueObject>(Object);
				};
				const auto Bytes = [&](uint64 Count) -> TSharedPtr<FJsonValue>
				{
					if (!Need(Count))
					{
						return MakeShared<FJsonValueNull>();
					}
					const FString Encoded = FBase64::Encode(At, uint32(Count));
					At += Count;
					return MakeShared<FJsonValueString>(Encoded);
				};
				if (Tag <= 0x7f)
				{
					return MakeShared<FJsonValueNumber>(Tag);
				}
				if (Tag >= 0xe0)
				{
					return MakeShared<FJsonValueNumber>(int8(Tag));
				}
				if ((Tag & 0xf0) == 0x80)
				{
					return Map(Tag & 0x0f);
				}
				if ((Tag & 0xf0) == 0x90)
				{
					return Array(Tag & 0x0f);
				}
				if ((Tag & 0xe0) == 0xa0)
				{
					return MakeShared<FJsonValueString>(Text(Tag & 0x1f));
				}
				switch (Tag)
				{
				case 0xc0:
					return MakeShared<FJsonValueNull>();
				case 0xc2:
					return MakeShared<FJsonValueBoolean>(false);
				case 0xc3:
					return MakeShared<FJsonValueBoolean>(true);
				case 0xc4:
					return Bytes(Unsigned(1));
				case 0xc5:
					return Bytes(Unsigned(2));
				case 0xc6:
					return Bytes(Unsigned(4));
				case 0xca:
				{
					const uint32 Bits = uint32(Unsigned(4));
					float Number;
					FMemory::Memcpy(&Number, &Bits, sizeof(Number));
					return MakeShared<FJsonValueNumber>(Number);
				}
				case 0xcb:
				{
					const uint64 Bits = Unsigned(8);
					double Number;
					FMemory::Memcpy(&Number, &Bits, sizeof(Number));
					return MakeShared<FJsonValueNumber>(Number);
				}
				case 0xcc:
					return MakeShared<FJsonValueNumber>(double(Unsigned(1)));
				case 0xcd:
					return MakeShared<FJsonValueNumber>(double(Unsigned(2)));
				case 0xce:
					return MakeShared<FJsonValueNumber>(double(Unsigned(4)));
				case 0xcf:
					return MakeShared<FJsonValueNumber>(double(Unsigned(8)));
				case 0xd0:
					return MakeShared<FJsonValueNumber>(double(int8(Unsigned(1))));
				case 0xd1:
					return MakeShared<FJsonValueNumber>(double(int16(Unsigned(2))));
				case 0xd2:
					return MakeShared<FJsonValueNumber>(double(int32(Unsigned(4))));
				case 0xd3:
					return MakeShared<FJsonValueNumber>(double(int64(Unsigned(8))));
				case 0xd9:
					return MakeShared<FJsonValueString>(Text(Unsigned(1)));
				case 0xda:
					return MakeShared<FJsonValueString>(Text(Unsigned(2)));
				case 0xdb:
					return MakeShared<FJsonValueString>(Text(Unsigned(4)));
				case 0xdc:
					return Array(Unsigned(2));
				case 0xdd:
					return Array(Unsigned(4));
				case 0xde:
					return Map(Unsigned(2));
				case 0xdf:
					return Map(Unsigned(4));
				default:
					bBad = true;
					return MakeShared<FJsonValueNull>();
				}
			}
		};
	} // namespace

	FString MessagePackToJson(const void* Data, SIZE_T Size)
	{
		if (!Data || !Size)
		{
			return TEXT("null");
		}
		FReader Reader{static_cast<const uint8*>(Data), static_cast<const uint8*>(Data) + Size};
		const TSharedPtr<FJsonValue> Value = Reader.Value(0);
		if (Reader.bBad)
		{
			return TEXT("null");
		}
		FString Out;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Value, FString(), Writer);
		return Out;
	}
} // namespace WeftValue
