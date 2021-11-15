#include <Luna.hpp>
#include <memory>

using namespace Luna;

class EditorApp : public App {
 public:
	EditorApp() : App("Editor") {}
	~EditorApp() {}

	virtual void Start() override {
		Log::Info("Starting Editor app.");
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

	std::unique_ptr<Engine> engine = std::make_unique<Engine>();
	engine->SetApp(app.get());

	return engine->Run();
}
