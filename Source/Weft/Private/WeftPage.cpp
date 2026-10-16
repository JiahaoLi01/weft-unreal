#include "WeftTypes.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

bool FWeftPage::FromDirectory(const FString& Directory, FWeftPage& Out, FString* OutError)
{
	Out = {};
	const auto Fail = [&](const FString& Why)
	{
		if (OutError)
		{
			*OutError = Why;
		}
		return false;
	};
	const auto Read = [&](const FString& Name, FString& Text)
	{
		return FFileHelper::LoadFileToString(Text, *FPaths::Combine(Directory, Name));
	};
	FString Manifest;
	if (Read(TEXT("page.json"), Manifest))
	{
		TSharedPtr<FJsonObject> Json;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Manifest), Json) || !Json)
		{
			return Fail(FString::Printf(TEXT("%s/page.json is not a JSON object"), *Directory));
		}
		TArray<FString> Scripts;
		Json->TryGetStringArrayField(TEXT("scripts"), Scripts);
		for (const FString& Script : Scripts)
		{
			FString Source;
			if (!Read(Script, Source))
			{
				return Fail(FString::Printf(TEXT("%s/%s is missing"), *Directory, *Script));
			}
			Out.Scripts.Emplace(Script, MoveTemp(Source));
		}
		FString Stylesheet;
		if (Json->TryGetStringField(TEXT("stylesheet"), Stylesheet) && !Read(Stylesheet, Out.Stylesheet))
		{
			return Fail(FString::Printf(TEXT("%s/%s is missing"), *Directory, *Stylesheet));
		}
		FString Keyframes;
		if (Json->TryGetStringField(TEXT("keyframes"), Keyframes) && !Read(Keyframes, Out.Keyframes))
		{
			return Fail(FString::Printf(TEXT("%s/%s is missing"), *Directory, *Keyframes));
		}
		Json->TryGetStringField(TEXT("root"), Out.Root);
		Json->TryGetBoolField(TEXT("crpgBridge"), Out.bCrpgBridge);
		Out.Base = Directory / (Stylesheet.IsEmpty() ? FString(TEXT("page.css")) : Stylesheet);
	}
	else
	{
		FString Source;
		if (!Read(TEXT("page.js"), Source))
		{
			return Fail(FString::Printf(TEXT("%s has neither page.json nor page.js"), *Directory));
		}
		Out.Scripts.Emplace(TEXT("page.js"), MoveTemp(Source));
		Read(TEXT("page.css"), Out.Stylesheet);
		Read(TEXT("keyframes.css"), Out.Keyframes);
		Out.Base = Directory / TEXT("page.css");
	}
	if (Out.Scripts.IsEmpty())
	{
		return Fail(FString::Printf(TEXT("%s names no script"), *Directory));
	}
	return true;
}
