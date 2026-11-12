// The plugin's debug tools with weft's dev build: the DLL is the dev build, weft's developer tools listen and list a View's
// page where the plugin says Chrome finds it, and the table of Views (page, size, elements, errors and warnings, in
// LogWeftPage after the page's name).
//
//   UnrealEditor-Cmd <project> -ExecCmds="Automation RunTests Weft.DevTools; Quit" -nullrhi -unattended

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_WEFT_DEVTOOLS

#include "IPAddress.h"
#include "SocketSubsystem.h"
#include "Sockets.h"
#include "WeftDebugTools.h"
#include "WeftSubsystem.h"
#include "WeftTypes.h"
#include "WeftView.h"
#include "weft/weft.h"

// The module's files share unity translation units: each test file keeps its helpers in a namespace of its own.
namespace WeftDevToolsTests
{
namespace
{
	// A page of a root and three boxes, in a folder named DevToolsTest (its base: the page's name).
	const TCHAR* const Script = TEXT(
		"const root = document.getElementById('root');"
		"for (let i = 0; i < 3; ++i) root.appendChild(document.createElement('div')).className = 'box';");
	const TCHAR* const Css = TEXT("#root{position:relative;width:100%;height:100%}.box{width:8px;height:8px}");

	// A View of a page, kept from the garbage collector.
	struct FPage
	{
		UWeftView* View = nullptr;

		explicit FPage(const FString& Folder, const FString& PageScript = Script, const FString& PageCss = Css)
		{
			View = UWeftSubsystem::Get()->CreateView();
			View->AddToRoot();
			View->Resize(FVector2D(64, 32), 1);
			FWeftPage Page;
			Page.Scripts.Emplace(TEXT("page.js"), PageScript);
			Page.Stylesheet = PageCss;
			Page.bForgivingCss = true;
			Page.Base = FString::Printf(TEXT("F:/weft-tests/%s/page.css"), *Folder);
			View->Load(Page);
			View->Advance();
		}

		~FPage()
		{
			View->RemoveFromRoot();
		}

		const FWeftViewDebugInfo* Find(const TArray<FWeftViewDebugInfo>& Views) const
		{
			return Views.FindByPredicate([this](const FWeftViewDebugInfo& Info) { return Info.View.Get() == View; });
		}
	};

	// What the developer tools' socket answers a GET of a path with, head and all; empty when nothing listens.
	FString HttpGet(int32 Port, const FString& Path)
	{
		ISocketSubsystem* Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		FSocket* Socket = Sockets ? Sockets->CreateSocket(NAME_Stream, TEXT("WeftDevToolsTest"), false) : nullptr;
		if (!Socket)
		{
			return FString();
		}
		const TSharedRef<FInternetAddr> Address = Sockets->CreateInternetAddr();
		bool bValid = false;
		Address->SetIp(TEXT("127.0.0.1"), bValid);
		Address->SetPort(Port);
		FString Answer;
		if (Socket->Connect(*Address))
		{
			const FTCHARToUTF8 Request(*FString::Printf(TEXT("GET %s HTTP/1.1\r\nHost: 127.0.0.1:%d\r\n\r\n"), *Path, Port));
			int32 Sent = 0;
			Socket->Send(reinterpret_cast<const uint8*>(Request.Get()), Request.Length(), Sent);
			TArray<uint8> Bytes;
			uint8 Chunk[4096];
			// The server closes the connection after its answer.
			while (Socket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromSeconds(5)))
			{
				int32 Read = 0;
				if (!Socket->Recv(Chunk, sizeof(Chunk), Read) || Read <= 0)
				{
					break;
				}
				Bytes.Append(Chunk, Read);
			}
			const FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()), Bytes.Num());
			Answer = FString(Text.Length(), Text.Get());
		}
		Socket->Close();
		Sockets->DestroySocket(Socket);
		return Answer;
	}
} // namespace
} // namespace WeftDevToolsTests

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeftDevToolsBuildTest, "Weft.DevTools.Build",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeftDevToolsBuildTest::RunTest(const FString& Parameters)
{
	using namespace WeftDevToolsTests;
	weft_build_info Build{sizeof(Build)};
	TestEqual(TEXT("weft_get_build_info"), int32(weft_get_build_info(&Build)), int32(WEFT_OK));
	TestTrue(TEXT("the plugin runs weft's dev build"), (Build.flags & WEFT_BUILD_DEVTOOLS) != 0);
	TestEqual(TEXT("the ABI the plugin was built with"), int32(Build.abi_version), int32(WEFT_ABI_VERSION));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeftDevToolsServeTest, "Weft.DevTools.Serve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeftDevToolsServeTest::RunTest(const FString& Parameters)
{
	using namespace WeftDevToolsTests;
	if (!UWeftSubsystem::Get() || !UWeftSubsystem::Get()->IsAvailable())
	{
		AddError(TEXT("weft does not run here"));
		return false;
	}
	// The developer tools of whoever runs the tests stay as they are.
	const bool bWereListening = FWeftDebugTools::GetDevToolsPort() != 0;
	if (!bWereListening)
	{
		AddExpectedMessagePlain(TEXT("Weft DevTools: listening on 127.0.0.1:"), ELogVerbosity::Display);
		TestTrue(TEXT("weft.DevTools starts"), FWeftDebugTools::StartDevTools());
	}
	const int32 Port = FWeftDebugTools::GetDevToolsPort();
	TestTrue(TEXT("a port"), Port > 0);
	TestEqual(TEXT("the address"), FWeftDebugTools::GetDevToolsAddress(), FString::Printf(TEXT("127.0.0.1:%d"), Port));

	// A page loaded while they listen: its address in the log, and in the table.
	AddExpectedMessagePlain(TEXT("Weft DevTools: [DevToolsTest] devtools://devtools/bundled/devtools_app.html?ws=127.0.0.1:"),
		ELogVerbosity::Display);
	const FPage Page(TEXT("DevToolsTest"));
	TArray<FWeftViewDebugInfo> Views;
	FWeftDebugTools::GetViews(Views);
	const FWeftViewDebugInfo* Info = Page.Find(Views);
	if (!TestNotNull(TEXT("the View in the table"), Info))
	{
		return false;
	}
	TestEqual(TEXT("the page's address for Chrome"), Info->DevToolsUrl,
		FString::Printf(TEXT("devtools://devtools/bundled/devtools_app.html?ws=127.0.0.1:%d/devtools/page/%s"), Port, *Info->Target));

	// weft lists the page under the target the plugin names: the address works.
	const FString List = HttpGet(Port, TEXT("/json/list"));
	TestTrue(TEXT("/json/list answers"), List.StartsWith(TEXT("HTTP/1.1 200")));
	TestTrue(FString::Printf(TEXT("/json/list lists /devtools/page/%s: %s"), *Info->Target, *List),
		List.Contains(FString::Printf(TEXT("/devtools/page/%s\""), *Info->Target)));
	TestTrue(TEXT("/json/version names weft"), HttpGet(Port, TEXT("/json/version")).Contains(TEXT("\"Browser\":\"Weft/")));

	// Started twice, they say they listen already.
	AddExpectedErrorPlain(TEXT("Weft DevTools: they listen on 127.0.0.1:"));
	FString Why;
	TestFalse(TEXT("a second start"), FWeftDebugTools::StartDevTools(TEXT("0"), &Why));
	TestTrue(TEXT("why"), Why.Contains(TEXT("already")));
	// What is not [address:]port.
	AddExpectedErrorPlain(TEXT("is not [127.0.0.1:|0.0.0.0:]port"), EAutomationExpectedErrorFlags::Contains, 2);
	TestFalse(TEXT("an unknown address"), FWeftDebugTools::StartDevTools(TEXT("10.0.0.1:9230"), &Why));
	TestFalse(TEXT("a port past 65535"), FWeftDebugTools::StartDevTools(TEXT("70000"), &Why));

	if (!bWereListening)
	{
		AddExpectedMessagePlain(TEXT("Weft DevTools: stopped."), ELogVerbosity::Display);
		FWeftDebugTools::StopDevTools();
		TestEqual(TEXT("stopped"), FWeftDebugTools::GetDevToolsPort(), 0);
		TestTrue(TEXT("nothing listens on the port"), HttpGet(Port, TEXT("/json/version")).IsEmpty());
		FWeftDebugTools::GetViews(Views);
		Info = Page.Find(Views);
		TestTrue(TEXT("no address while they do not listen"), Info && Info->DevToolsUrl.IsEmpty());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeftDevToolsViewsTest, "Weft.DevTools.Views",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWeftDevToolsViewsTest::RunTest(const FString& Parameters)
{
	using namespace WeftDevToolsTests;
	if (!UWeftSubsystem::Get() || !UWeftSubsystem::Get()->IsAvailable())
	{
		AddError(TEXT("weft does not run here"));
		return false;
	}
	const FPage Boxes(TEXT("Boxes"));
	TestEqual(TEXT("the page's name is its folder"), Boxes.View->GetPageName(), FString(TEXT("Boxes")));

	// A page that throws, and one whose scene cannot take a style: in LogWeftPage after their names, and counted.
	AddExpectedErrorPlain(TEXT("[Thrower] "), EAutomationExpectedErrorFlags::Contains, 0);
	AddExpectedErrorPlain(TEXT("Weft: load a page:"), EAutomationExpectedErrorFlags::Contains, 0);
	const FPage Thrower(TEXT("Thrower"), TEXT("throw new Error('on purpose');"));
	AddExpectedErrorPlain(TEXT("[Dropper] "), EAutomationExpectedErrorFlags::Contains, 0);
	const FPage Dropper(TEXT("Dropper"), Script, FString(Css) + TEXT(".box{column-count:2}"));

	TArray<FWeftViewDebugInfo> Views;
	FWeftDebugTools::GetViews(Views);
	const FWeftViewDebugInfo* Info = Boxes.Find(Views);
	if (!TestNotNull(TEXT("the View in the table"), Info))
	{
		return false;
	}
	TestEqual(TEXT("page"), Info->Page, FString(TEXT("Boxes")));
	TestTrue(TEXT("size"), Info->Size == FVector2D(64, 32));
	TestTrue(TEXT("target: the View's slot and generation"), Info->Target.Contains(TEXT("-")));
	// The root and the three boxes, inside the document's own elements.
	const FWeftViewDebugInfo* Thrown = Thrower.Find(Views);
	TestTrue(FString::Printf(TEXT("elements: %d"), Info->Elements), Info->Elements >= 4);
	TestEqual(TEXT("no errors"), Info->Errors, 0);
	TestEqual(TEXT("no warnings"), Info->Warnings, 0);
	TestTrue(TEXT("the throwing page's error"), Thrown && Thrown->Errors >= 1);
	const FWeftViewDebugInfo* Dropped = Dropper.Find(Views);
	TestTrue(TEXT("the dropped style's warning"), Dropped && Dropped->Warnings >= 1);

	const TArray<FString> Lines = FWeftDebugTools::FormatViews(Views);
	TestEqual(TEXT("a header and a line a View"), Lines.Num(), Views.Num() + 1);
	TestTrue(TEXT("the header"), Lines.Num() > 0 && Lines[0].StartsWith(TEXT("View")) && Lines[0].Contains(TEXT("Elements")));
	TestTrue(TEXT("the page's line"), Lines.ContainsByPredicate([](const FString& Line) { return Line.Contains(TEXT("Boxes")); }));
	return true;
}

#endif
