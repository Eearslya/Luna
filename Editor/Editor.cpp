#include <Luna.hpp>
#include <memory>

using namespace Luna;

class EditorApp : public App {
 public:
	EditorApp() : App("Editor") {}
	~EditorApp() {}

	virtual void Start() override {
		Log::Info("Starting Editor app.");

		Window::Get()->Maximize();

		Filesystem::Get()->AddSearchPath("Assets");

		Graphics::Get()->SetEditorLayout(true);

		Timers::Get()->Every(Time::Seconds(1), []() {
			const auto fps          = Engine::Get()->GetFPS();
			const auto ups          = Engine::Get()->GetUPS();
			const std::string title = fmt::format("Luna - {} FPS | {} UPS", fps, ups);
			Window::Get()->SetTitle(title);
		});

		Keyboard::Get()->OnKey().Add(
			[](Key key, InputAction action, InputMods mods, bool uiCapture) -> bool {
				if (key == Key::F1 && action == InputAction::Press) {
					Graphics::Get()->SetEditorLayout(!Graphics::Get()->IsEditorLayout());
				}
				if (key == Key::Escape && action == InputAction::Press) {
					Engine::Get()->Shutdown();
					return true;
				}

				return false;
			},
			this);

		auto& scene = Graphics::Get()->GetScene();
		scene.LoadEnvironment("Environments/TokyoBigSight.hdr");
		scene.LoadModel("Models/TestScene/TestScene.gltf", scene.GetRoot());
	}

	virtual void Update() override {}

	virtual void Stop() override {
		Log::Info("Stopping Editor app.");
	}
};

int main(int argc, const char** argv) {
#ifdef LUNA_DEBUG
	Log::SetLevel(spdlog::level::trace);
#endif

	std::unique_ptr<EditorApp> app = std::make_unique<EditorApp>();

	std::unique_ptr<Engine> engine = std::make_unique<Engine>(argv[0]);
	engine->SetApp(app.get());

	return engine->Run();
}
