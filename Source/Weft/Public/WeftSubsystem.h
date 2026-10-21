#pragma once

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"

#include "WeftSubsystem.generated.h"

class FTextureResource;
class FWeftRenderer;
class UTexture;
class UWeftView;
struct weft_runtime;

/**
 * The weft runtime of the engine: the Views (UWeftView) on the game thread, which owns them, and the renderer that
 * draws their frames with weft's backend of the running RHI (one renderer draws the Views of one runtime).
 */
UCLASS()
class WEFT_API UWeftSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	static UWeftSubsystem* Get();

	/** A new View of logical size 1 x 1 until its widget sizes it; null when weft does not run here. */
	UFUNCTION(BlueprintCallable, Category = "Weft")
	UWeftView* CreateView();

	bool IsAvailable() const { return Runtime != nullptr; }

	/**
	 * A font file the pages may use by Family (CSS font-family), with its weight (1-1000), style, face in a collection
	 * and unicode-range (CSS text, "U+0030-0039"; empty: every code point); before the runtime's first page loads (false
	 * after, or for a file weft cannot open). The platform's installed fonts are there already.
	 */
	bool AddFont(const FString& Family, const FString& File, int32 Weight = 400, bool bItalic = false, int32 FaceIndex = 0,
		const FString& UnicodeRange = FString());
	/** A family tried for characters no family of an element's font-family list covers; before the first page loads. */
	bool AddFallbackFamily(const FString& Family);
	/**
	 * The fonts a page lists in a manifest ({"faces": [{"family", "path", "weight", "italic", "face", "unicodeRange"}],
	 * "fallback": [...]}, paths relative to the file): every face and fallback family, once a process (a manifest
	 * added already is skipped). The number of faces added; before the first page loads.
	 */
	int32 AddFonts(const FString& ManifestFile);

	/**
	 * The UI shader effects a page lists in a manifest ({"effects": [{"name", "bytecode", "animated", "backdrop",
	 * "backdropMargin", "parameters": [{"name", "type", "initial"}], "textures": [{"name", "path", "format"}]}]}, paths
	 * relative to the file): each effect's bytecode (<bytecode>.encoded.dxbc, .linear.dxbc, .encoded.spv, .linear.spv,
	 * which weft's cmake/compile_effect.cmake writes) and PNG textures ("data": the red channel; "srgb": color),
	 * registered for every View's pages (-weft-effect(name)). "backdrop": the effect reads what lies behind its element
	 * (refraction, heat haze; weft's docs/effects.md), from a copy reaching "backdropMargin" CSS px past its box. An
	 * effect registered already is skipped. The number of effects added.
	 */
	int32 AddEffects(const FString& ManifestFile);

	/**
	 * A texture of the engine that pages draw by name: CSS `background-image: -weft-texture(Name)` (with
	 * background-size, -position, -repeat and the rest), on every View. Null takes the name away. Names are letters,
	 * digits, '-' and '_', at most 64 (false for another). A render target may be drawn again every frame (a scene
	 * capture's, before the View draws the page): weft samples it as a live texture. Any other texture's contents must
	 * stay as they are while it is named (a frozen texture); one that changes is named anew. Linear or sRGB RGBA8 and
	 * BGRA8, and RGBA16F, are drawn; their colours are linear and premultiplied. Game thread.
	 */
	bool SetTexture(const FString& Name, UTexture* Texture);
	/** The named textures whose resources exist now, for a frame's draw (game thread). */
	void GetTextures(TArray<TPair<FString, FTextureResource*>>& Out) const;

	/** The render side, shared with the widgets' draws. */
	TSharedPtr<FWeftRenderer, ESPMode::ThreadSafe> GetRenderer() const { return Renderer; }

private:
	weft_runtime* Runtime = nullptr;
	TSharedPtr<FWeftRenderer, ESPMode::ThreadSafe> Renderer;
	TArray<TWeakObjectPtr<UWeftView>> Views;
	TMap<FString, TWeakObjectPtr<UTexture>> Textures;
	// The font manifests added, and the effects registered.
	TSet<FString> FontManifests;
	TSet<FString> Effects;
};
