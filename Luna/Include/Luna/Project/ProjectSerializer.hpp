#pragma once

namespace Luna {
class Path;
class Project;

class ProjectSerializer {
 public:
	ProjectSerializer(Project& project);

	bool Deserialize(const Path& filePath);
	bool Serialize(const Path& filePath);

 private:
	Project& _project;
};
}  // namespace Luna
