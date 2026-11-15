#include "BlackHoleEffect.h"

#define NOMINMAX
#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <numbers>

namespace
{
	constexpr float kTau = 2.0f * std::numbers::pi_v<float>;
	constexpr uint32_t kDiskSparkCount = 96;
	constexpr std::array<std::array<float, 3>, 6> kOrbitColors{{
		{ 0.17f, 0.76f, 1.0f }, { 0.46f, 0.27f, 1.0f }, { 0.39f, 0.53f, 1.0f },
		{ 0.89f, 0.38f, 1.0f }, { 0.96f, 0.97f, 1.0f }, { 1.0f, 0.69f, 0.25f },
	}};
	constexpr std::array<std::array<float, 3>, 6> kExplosionColors{{
		{ 1.0f, 0.35f, 0.09f }, { 1.0f, 0.64f, 0.15f }, { 1.0f, 0.94f, 0.68f },
		{ 0.33f, 0.81f, 1.0f }, { 0.67f, 0.38f, 1.0f }, { 1.0f, 0.48f, 0.73f },
	}};

	float SmoothStep(float from, float to, float value)
	{
		const float t = std::clamp((value - from) / (to - from), 0.0f, 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}

	bool IsOrbitStar(uint32_t index)
	{
		return index % 3 == (index / 3) % 3;
	}

	bool IsValid(const BlackHoleSettings& settings)
	{
		return settings.orbitCount >= BlackHoleSettings::kMinOrbitCount &&
			settings.orbitCount <= BlackHoleSettings::kMaxOrbitCount &&
			settings.pullDuration >= BlackHoleSettings::kMinPullDuration &&
			settings.pullDuration <= BlackHoleSettings::kMaxPullDuration &&
			std::all_of(settings.shockwaveColor.begin(), settings.shockwaveColor.end(),
				[](float channel) { return channel >= 0.0f && channel <= 1.0f; });
	}
}

bool LoadBlackHoleSettings(const std::filesystem::path& path, BlackHoleSettings& settings)
{
	std::ifstream input(path);
	input.imbue(std::locale::classic());
	BlackHoleSettings loaded;
	std::string header, countKey, durationKey, colorKey;
	if (!(input >> header >> countKey >> loaded.orbitCount >> durationKey >> loaded.pullDuration
		>> colorKey >> loaded.shockwaveColor[0] >> loaded.shockwaveColor[1] >> loaded.shockwaveColor[2]) ||
		header != "BlackHoleSettings/1" || countKey != "OrbitCount" || durationKey != "PullDuration" ||
		colorKey != "ShockwaveColor" || !IsValid(loaded))
	{
		return false;
	}
	input >> std::ws;
	if (!input.eof() || input.bad())
	{
		return false;
	}
	settings = loaded;
	return true;
}

bool SaveBlackHoleSettings(const std::filesystem::path& path, const BlackHoleSettings& settings)
{
	if (!IsValid(settings))
	{
		return false;
	}
	auto temporaryPath = path;
	temporaryPath += L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
	std::ofstream output(temporaryPath, std::ios::trunc);
	output.imbue(std::locale::classic());
	output << std::setprecision(std::numeric_limits<float>::max_digits10)
		<< "BlackHoleSettings/1\nOrbitCount " << settings.orbitCount
		<< "\nPullDuration " << settings.pullDuration << "\nShockwaveColor "
		<< settings.shockwaveColor[0] << ' ' << settings.shockwaveColor[1] << ' ' << settings.shockwaveColor[2] << '\n';
	output.close();
	if (!output || !MoveFileExW(temporaryPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
	{
		std::error_code error;
		std::filesystem::remove(temporaryPath, error);
		return false;
	}
	return true;
}

void BlackHoleEffect::Start(std::mt19937& randomEngine, const BlackHoleSettings& settings)
{
	constexpr auto maxOrbits = BlackHoleSettings::kMaxOrbitCount;
	static_assert(1 + kDiskSparkCount + maxOrbits + (maxOrbits + 2) / 3 <= kMaxSprites);
	static_assert(6 + kShardCount + kShardCount / 4 <= kMaxSprites);
	settings_ = IsValid(settings) ? settings : BlackHoleSettings{};
	orbitSeeds_.resize(static_cast<size_t>(settings_.orbitCount));
	sprites_.reserve(kMaxSprites);
	sprites_.clear();
	elapsedTime_ = 0.0f;
	active_ = true;

	const float timeScale = settings_.pullDuration / BlackHoleSettings{}.pullDuration;
	std::uniform_real_distribution<float> randomUnit(0.0f, 1.0f);
	for (uint32_t i = 0; i < orbitSeeds_.size(); ++i)
	{
		OrbitSeed& seed = orbitSeeds_[i];
		seed.radius = std::lerp(0.27f, 0.76f, randomUnit(randomEngine));
		seed.angle = kTau * static_cast<float>(i % 3) / 3.0f + seed.radius * 4.0f
			+ std::lerp(-0.65f, 0.65f, randomUnit(randomEngine));
		seed.releaseTime = std::lerp(0.0f, 0.86f, randomUnit(randomEngine)) * timeScale;
		seed.arrivalTime = std::lerp(1.64f, 3.03f, std::sqrt(randomUnit(randomEngine))) * timeScale;
		seed.angularTravel = std::lerp(3.4f, 5.1f, randomUnit(randomEngine));
		seed.spin = std::lerp(-3.6f, 3.6f, randomUnit(randomEngine));
		seed.size = IsOrbitStar(i) ? std::lerp(0.014f, 0.028f, randomUnit(randomEngine))
			: std::lerp(0.004f, 0.012f, randomUnit(randomEngine));
	}
	for (uint32_t i = 0; i < kShardCount; ++i)
	{
		ShardSeed& seed = shardSeeds_[i];
		seed.angle = kTau * randomUnit(randomEngine);
		seed.speed = std::lerp(0.18f, 1.22f, randomUnit(randomEngine));
		seed.releaseTime = std::lerp(0.0f, 0.15f, randomUnit(randomEngine));
		seed.lifeTime = std::lerp(0.64f, 1.62f, randomUnit(randomEngine));
		seed.spin = std::lerp(-6.0f, 6.0f, randomUnit(randomEngine));
		seed.size = (i % 6 == 0) ? std::lerp(0.012f, 0.026f, randomUnit(randomEngine))
			: std::lerp(0.003f, 0.012f, randomUnit(randomEngine));
	}
}

void BlackHoleEffect::Update(float deltaTime)
{
	if (active_)
	{
		elapsedTime_ += std::max(0.0f, deltaTime);
		active_ = elapsedTime_ < settings_.pullDuration + kExplosionDuration;
	}
}

bool BlackHoleEffect::IsActive() const
{
	return active_;
}

std::span<const BlackHoleSprite> BlackHoleEffect::BuildSprites(float width, float height)
{
	sprites_.clear();
	if (!IsActive() || width <= 0.0f || height <= 0.0f)
	{
		return {};
	}

	const Vector2 center{ width * 0.5f, height * 0.5f };
	const float unit = std::min(width, height);
	if (elapsedTime_ < settings_.pullDuration)
	{
		BuildPullSprites(center, unit);
	}
	else
	{
		BuildExplosionSprites(center, unit);
	}
	return sprites_;
}

void BlackHoleEffect::BuildPullSprites(const Vector2& center, float unit)
{
	const auto absorbedCount = std::count_if(orbitSeeds_.begin(), orbitSeeds_.end(), [&](const OrbitSeed& seed)
	{
		return elapsedTime_ >= seed.arrivalTime;
	});
	const float charge = static_cast<float>(absorbedCount) / static_cast<float>(orbitSeeds_.size());
	const float opening = SmoothStep(0.0f, 0.42f, elapsedTime_);
	const float coreSize = (0.24f + 0.075f * charge) * (0.55f + 0.45f * opening);
	const float horizon = coreSize * 0.28f;
	sprites_.push_back({ center, { coreSize * unit, coreSize * unit }, 0.0f,
		{ 0.43f, 0.59f, 1.0f, opening }, BlackHoleShape::Core, elapsedTime_, charge });

	// 高速で周回する小さな光で、中心付近の降着円盤を表す。
	for (uint32_t i = 0; i < kDiskSparkCount; ++i)
	{
		const float band = static_cast<float>(i % 19) / 18.0f;
		const float radius = coreSize * (0.32f + 0.34f * band);
		const float angle = kTau * static_cast<float>(i) / kDiskSparkCount + elapsedTime_ * (5.6f - 2.5f * band);
		const Vector2 offset{ std::cos(angle) * radius, std::sin(angle) * radius * 0.62f };
		const float intensity = opening * (0.3f + 0.35f * charge)
			* SmoothStep(horizon, horizon + 0.022f, std::hypot(offset.x, offset.y));
		const float size = (0.0034f + 0.002f * band) * unit;
		sprites_.push_back({ { center.x + offset.x * unit, center.y + offset.y * unit }, { size, size }, 0.0f,
			{ 0.38f, 0.65f, 1.0f, intensity }, BlackHoleShape::Spark, elapsedTime_ + band, charge });
	}

	for (uint32_t i = 0; i < orbitSeeds_.size(); ++i)
	{
		const OrbitSeed& seed = orbitSeeds_[i];
		if (elapsedTime_ < seed.releaseTime || elapsedTime_ >= seed.arrivalTime)
		{
			continue;
		}
		const float age = elapsedTime_ - seed.releaseTime;
		const float progress = age / (seed.arrivalTime - seed.releaseTime);
		const auto orbitOffset = [&](float t)
		{
			const float radius = std::lerp(seed.radius, 0.038f, t * (0.27f + 0.73f * t));
			const float angle = seed.angle + seed.angularTravel * t * (0.38f + 0.62f * t);
			return Vector2{ std::cos(angle) * radius, std::sin(angle) * radius * (0.91f - 0.17f * t) };
		};
		const Vector2 offset = orbitOffset(progress);
		const float intensity = SmoothStep(0.0f, 0.18f, age)
			* SmoothStep(horizon, horizon + 0.035f, std::hypot(offset.x, offset.y));
		const Vector2 position{ center.x + offset.x * unit, center.y + offset.y * unit };
		const auto& tint = kOrbitColors[(i / 3) % kOrbitColors.size()];
		const float size = seed.size * unit * (1.0f - 0.35f * progress);
		const bool star = IsOrbitStar(i);
		if (star)
		{
			const Vector2 previous = orbitOffset(std::max(0.0f, progress - 0.04f));
			const Vector2 travel{ (offset.x - previous.x) * unit, (offset.y - previous.y) * unit };
			const float length = std::hypot(travel.x, travel.y);
			sprites_.push_back({ { position.x - travel.x * 0.5f, position.y - travel.y * 0.5f },
				{ size + length * 0.65f, size * 0.33f }, std::atan2(travel.y, travel.x),
				{ tint[0], tint[1], tint[2], intensity * 0.42f }, BlackHoleShape::Trail, age, progress });
		}
		sprites_.push_back({ position, { size, size }, seed.angle + seed.spin * age,
			{ tint[0], tint[1], tint[2], intensity }, star ? BlackHoleShape::Star : BlackHoleShape::Spark, age, progress });
	}
}

void BlackHoleEffect::BuildExplosionSprites(const Vector2& center, float unit)
{
	const float time = elapsedTime_ - settings_.pullDuration;
	if (time < 0.20f)
	{
		const float coreFade = 1.0f - SmoothStep(0.0f, 0.20f, time);
		const float size = (0.15f + 0.165f * coreFade) * unit;
		sprites_.push_back({ center, { size, size }, 0.0f,
			{ 0.43f, 0.59f, 1.0f, coreFade }, BlackHoleShape::Core, elapsedTime_, 1.0f });
	}
	if (time < 0.42f)
	{
		const float flash = 1.0f - SmoothStep(0.0f, 0.42f, time);
		const float size = (0.32f + 0.52f * time) * unit;
		sprites_.push_back({ center, { size, size }, time * 0.8f,
			{ 0.78f, 0.87f, 1.0f, flash }, BlackHoleShape::Flash, time, 1.0f });
		const float whiteSize = (0.12f + 0.33f * time) * unit;
		sprites_.push_back({ center, { whiteSize, whiteSize }, 0.0f,
			{ 1.0f, 0.90f, 0.65f, flash * flash }, BlackHoleShape::Flash, time, 0.7f });
	}

	constexpr std::array<float, 3> ringDelay{ 0.0f, 0.10f, 0.24f };
	constexpr std::array<float, 3> ringSpeed{ 1.18f, 0.92f, 0.76f };
	for (uint32_t i = 0; i < ringDelay.size(); ++i)
	{
		const float age = time - ringDelay[i];
		if (age < 0.0f || age >= 0.94f)
		{
			continue;
		}
		const float radius = (0.082f + ringSpeed[i] * age) * unit;
		const float alpha = 0.82f * (1.0f - SmoothStep(0.11f, 0.94f, age));
		const auto& tint = i == 0 ? settings_.shockwaveColor : kExplosionColors[i + 2];
		sprites_.push_back({ center, { radius, radius }, 0.0f,
			{ tint[0], tint[1], tint[2], alpha }, BlackHoleShape::Ring, age, 1.0f - age });
	}

	for (uint32_t i = 0; i < kShardCount; ++i)
	{
		const ShardSeed& seed = shardSeeds_[i];
		const float age = time - seed.releaseTime;
		if (age < 0.0f || age >= seed.lifeTime)
		{
			continue;
		}
		const float progress = age / seed.lifeTime;
		const float distance = (0.022f + seed.speed * age / (1.0f + age * 0.95f)) * unit;
		const float x = std::cos(seed.angle);
		const float y = std::sin(seed.angle);
		const Vector2 position{ center.x + x * distance, center.y + y * distance + 0.065f * age * age * unit };
		const float intensity = 1.0f - SmoothStep(0.14f, 1.0f, progress);
		const auto& tint = kExplosionColors[(i + i / 6) % kExplosionColors.size()];
		const float size = seed.size * unit * (1.0f - 0.53f * progress);
		if (i % 4 == 0)
		{
			const float length = (0.015f + 0.048f * seed.speed) * intensity * unit;
			sprites_.push_back({ { position.x - x * length * 0.62f, position.y - y * length * 0.62f },
				{ length, size * 0.42f }, seed.angle, { tint[0], tint[1], tint[2], intensity * 0.58f },
				BlackHoleShape::Trail, age, 1.0f - progress });
		}
		const BlackHoleShape shape = (i % 6 == 0) ? BlackHoleShape::Star : BlackHoleShape::Spark;
		sprites_.push_back({ position, { size, size }, seed.angle + seed.spin * age,
			{ tint[0], tint[1], tint[2], intensity }, shape, age, 1.0f - progress });
	}
}
