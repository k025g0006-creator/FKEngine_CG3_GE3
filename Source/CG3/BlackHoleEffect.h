#pragma once

#include "Matrix4x4.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <random>
#include <span>
#include <vector>

enum class BlackHoleShape : uint32_t
{
	Core,
	Star,
	Spark,
	Ring,
	Flash,
	Trail,
};

struct BlackHoleSprite
{
	Vector2 position;
	Vector2 halfSize;
	float rotation;
	std::array<float, 4> color;
	BlackHoleShape shape;
	float time;
	float energy;
};

struct BlackHoleSettings
{
	static constexpr int kMinOrbitCount = 12;
	static constexpr int kMaxOrbitCount = 1200;
	static constexpr float kMinPullDuration = 0.5f;
	static constexpr float kMaxPullDuration = 12.0f;

	int orbitCount = 576;
	float pullDuration = 3.2f;
	std::array<float, 3> shockwaveColor{ 1.0f, 0.94f, 0.68f };
};

bool LoadBlackHoleSettings(const std::filesystem::path& path, BlackHoleSettings& settings);
bool SaveBlackHoleSettings(const std::filesystem::path& path, const BlackHoleSettings& settings);

class BlackHoleEffect
{
public:
	static constexpr uint32_t kMaxSprites = 2048;

	void Start(std::mt19937& randomEngine, const BlackHoleSettings& settings = {});
	void Update(float deltaTime);
	bool IsActive() const;
	std::span<const BlackHoleSprite> BuildSprites(float width, float height);

private:
	struct OrbitSeed
	{
		float angle;
		float radius;
		float releaseTime;
		float arrivalTime;
		float angularTravel;
		float spin;
		float size;
	};

	struct ShardSeed
	{
		float angle;
		float speed;
		float releaseTime;
		float lifeTime;
		float spin;
		float size;
	};

	static constexpr uint32_t kShardCount = 900;
	static constexpr float kExplosionDuration = 1.8f;

	void BuildPullSprites(const Vector2& center, float unit);
	void BuildExplosionSprites(const Vector2& center, float unit);

	std::vector<OrbitSeed> orbitSeeds_;
	std::array<ShardSeed, kShardCount> shardSeeds_{};
	std::vector<BlackHoleSprite> sprites_;
	BlackHoleSettings settings_;
	float elapsedTime_ = 0.0f;
	bool active_ = false;
};
