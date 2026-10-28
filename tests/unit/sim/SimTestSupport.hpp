#pragma once

// Helpers for the sim unit tests (no asserts here: they only work inside UNIT_TEST bodies).

#include "kestrel/base/Bytes.hpp"
#include "kestrel/knx/Apdu.hpp"
#include "kestrel/knx/Clock.hpp"
#include "kestrel/knx/KnxIp.hpp"
#include "kestrel/knx/TunnelClient.hpp"
#include "kestrel/sim/SimLink.hpp"
#include "kestrel/sim/TunnelServer.hpp"
#include "kestrel/sim/VirtualBus.hpp"
#include "kestrel/sim/VirtualDevice.hpp"

#include <functional>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

namespace simtest
{

using namespace kestrel;

inline Bytes hex(std::string_view text)
{
	return fromHex(text).value();
}

inline const knx::TimePoint kT0 = knx::TimePoint{} + std::chrono::hours(1);

template <typename T, typename Action>
std::vector<T> only(const std::vector<Action>& actions)
{
	std::vector<T> out;
	for (const auto& action : actions)
	{
		if (const auto* value = std::get_if<T>(&action))
		{
			out.push_back(*value);
		}
	}
	return out;
}

template <typename T>
std::optional<T> knxipAs(const Bytes& bytes)
{
	auto frame = knx::knxip::decode(bytes);
	if (frame)
	{
		if (const auto* value = std::get_if<T>(&*frame))
		{
			return *value;
		}
	}
	return std::nullopt;
}

/// "switch-a": 15.15.255, serial 0083:00001234, a DPT 1 switch object on 1/1/1 (CWTU) and a
/// readable DPT 9 temperature on 1/1/2 (CRT).
inline sim::DeviceConfig switchConfig(bool progMode = false)
{
	sim::DeviceConfig config;
	config.name = "switch-a";
	config.serial = knx::SerialNumber::parse("0083:00001234").value();
	config.progMode = progMode;
	config.groupObjects.push_back(sim::GroupObjectConfig{knx::GroupAddress::parse("1/1/1").value(),
		sim::GroupObjectFlags::parse("CWTU").value(), dpt::DptId{1, 1}, hex("00")});
	config.groupObjects.push_back(sim::GroupObjectConfig{knx::GroupAddress::parse("1/1/2").value(),
		sim::GroupObjectFlags::parse("CRT").value(), dpt::DptId{9, 1}, hex("0c 33")});
	return config;
}

/// Executes a sans-IO machine's SendFrame actions on `link` until it goes quiet; the other
/// actions are appended to `out`. Works for IaProgrammer and ManagementSession.
template <typename Machine>
void drive(Machine& machine, sim::SimLink& link, std::vector<typename Machine::Action>& out)
{
	for (;;)
	{
		auto actions = machine.takeActions();
		if (actions.empty())
		{
			return;
		}
		for (auto& action : actions)
		{
			if (auto* send = std::get_if<typename Machine::SendFrame>(&action))
			{
				(void)link.send(send->frame);
			}
			else
			{
				out.push_back(std::move(action));
			}
		}
	}
}

/// TunnelClient ⇄ TunnelServer ⇄ VirtualBus, fully in-process, with a manual clock.
struct TunnelRig
{
	sim::VirtualBus bus;
	sim::TunnelServer server{bus};
	knx::TunnelClient client;
	knx::TimePoint now = kT0;
	knx::knxip::IpEndpoint clientEndpoint{{127, 0, 0, 1}, 50123};

	std::vector<knx::TunnelClient::Action> events;  ///< client actions other than datagrams
	std::vector<knx::cemi::LData> received;          ///< L_Data.ind delivered to the client
	std::function<void(const knx::cemi::LData&)> onFrame;
	std::size_t datagrams = 0;

	/// Shuttles datagrams between client and server until both are quiet.
	void pump()
	{
		for (bool moved = true; moved;)
		{
			moved = false;
			for (auto& action : client.takeActions())
			{
				if (auto* control = std::get_if<knx::TunnelClient::SendControl>(&action))
				{
					server.onDatagram(clientEndpoint, control->bytes, now);
					moved = true;
					++datagrams;
				}
				else if (auto* data = std::get_if<knx::TunnelClient::SendData>(&action))
				{
					server.onDatagram(clientEndpoint, data->bytes, now);
					moved = true;
					++datagrams;
				}
				else
				{
					if (auto* frame = std::get_if<knx::TunnelClient::FrameReceived>(&action))
					{
						received.push_back(frame->frame);
						if (onFrame)
						{
							onFrame(frame->frame);
						}
					}
					events.push_back(std::move(action));
				}
			}
			for (auto& action : server.takeActions())
			{
				if (auto* send = std::get_if<sim::TunnelServer::SendDatagram>(&action))
				{
					client.onDatagram(send->bytes, now);
					moved = true;
					++datagrams;
				}
			}
		}
	}

	void connect()
	{
		client.connect(now);
		pump();
	}

	void send(const knx::cemi::LData& frame)
	{
		(void)client.send(frame, now);
		pump();
	}

	/// Advances the clock in `step`s, ticking client, server and the given devices.
	void advance(knx::Duration total, std::vector<sim::VirtualDevice*> devices = {},
		knx::Duration step = std::chrono::milliseconds(100))
	{
		for (knx::Duration elapsed{}; elapsed < total; elapsed += step)
		{
			now += step;
			client.tick(now);
			server.tick(now);
			for (auto* device : devices)
			{
				device->tick(now);
			}
			pump();
		}
	}

	/// Runs a sans-IO machine (IaProgrammer / ManagementSession) over the tunnel until `done()`
	/// or `limit` elapses; non-frame actions go to `out`.
	template <typename Machine>
	void run(Machine& machine, std::vector<typename Machine::Action>& out, const std::function<bool()>& done,
		std::vector<sim::VirtualDevice*> devices = {}, knx::Duration limit = std::chrono::seconds(30))
	{
		onFrame = [&machine, this](const knx::cemi::LData& frame) { machine.onFrame(frame, now); };
		const auto deadline = now + limit;
		auto flush = [&]
		{
			for (;;)
			{
				auto actions = machine.takeActions();
				if (actions.empty())
				{
					return;
				}
				for (auto& action : actions)
				{
					if (auto* send = std::get_if<typename Machine::SendFrame>(&action))
					{
						(void)client.send(send->frame, now);
					}
					else
					{
						out.push_back(std::move(action));
					}
				}
				pump(); // may feed answers back into the machine
			}
		};
		flush();
		while (!done() && now < deadline)
		{
			now += std::chrono::milliseconds(100);
			client.tick(now);
			server.tick(now);
			for (auto* device : devices)
			{
				device->tick(now);
			}
			machine.tick(now);
			pump();
			flush();
		}
		onFrame = nullptr;
	}
};

} // namespace simtest
