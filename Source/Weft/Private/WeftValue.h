#pragma once

#include "CoreMinimal.h"

/** The values weft hands the engine: MessagePack, read into JSON text (bytes as base64 strings, extension types and
 *  malformed input as null). */
namespace WeftValue
{
	FString MessagePackToJson(const void* Data, SIZE_T Size);
}
