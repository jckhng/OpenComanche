#include "keyboard_pilot.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
} // namespace

int main() {
    try {
        using namespace comanchecd;
        keyboard_test::Pilot pilot;
        SpatialFrame frame;
        frame.serial = 1;
        auto& target = frame.scene.entities[1];
        target.active = true;
        target.target = 1;
        target.y = std::uint32_t(-0x04000000);
        FlightState state;
        state.view.heading = 0x40000000;
        state.forward_speed = 0x01400000;
        state.ammunition = {500, 62, 8, 0, 0, 0};
        require(pilot.controls(state, frame).vertical > 0,
                "Losing lock inside stand-off range while turning must brake instead of accelerating into an "
                "orbit");
        const auto first = pilot.controls(state, frame);
        for (unsigned poll = 0; poll < 20; ++poll) {
            const auto repeated = pilot.controls(state, frame);
            require(repeated.horizontal == first.horizontal && repeated.vertical == first.vertical &&
                        repeated.cycle_target == first.cycle_target && repeated.fire == first.fire,
                    "Repeated presentation polls must retain the same native-frame key decision");
        }
        {
            keyboard_test::Pilot stable;
            SpatialFrame observed;
            observed.serial = 1;
            auto& chosen = observed.scene.entities[1];
            chosen.active = true;
            chosen.target = 1;
            chosen.y = std::uint32_t(-0x30000000);
            FlightState flight;
            flight.weapon = 2;
            flight.ammunition = state.ammunition;
            stable.controls(flight, observed);
            auto& closer = observed.scene.entities[2];
            closer = chosen;
            closer.x = 0x10000000;
            closer.y = 0;
            ++observed.serial;
            require(stable.controls(flight, observed).horizontal == 0,
                    "An unlocked pilot must retain its live objective when another becomes closer");
            chosen.phase = EntityPhase::explosion;
            flight.target.slot = 1;
            ++observed.serial;
            const auto retarget = stable.controls(flight, observed);
            require(retarget.horizontal > 0 && !retarget.fire,
                    "An exploding objective must release pursuit and suppress firing at its stale lock");
            flight.target.slot = 2;
            ++observed.serial;
            require(stable.controls(flight, observed).fire,
                    "A live lock and the selected weapon must permit a firing edge");
            ++observed.serial;
            require(!stable.controls(flight, observed).fire,
                    "Missile shots must include a released-key frame");
            closer.active = false;
            ++observed.serial;
            const auto complete = stable.controls(flight, observed);
            require(!complete.fire && !complete.cycle_target,
                    "Completed objectives must stop fire and target acquisition");
        }
        {
            keyboard_test::Pilot braking;
            SpatialFrame observed;
            observed.scene.entities[1] = target;
            FlightState flight = state;
            flight.view.heading = 0;
            flight.view.y = 0x20000000;
            for (unsigned tick = 0; tick < 12; ++tick) {
                observed.serial = tick;
                const auto input = braking.controls(flight, observed);
                require(input.horizontal == 0 && input.vertical <= 0,
                        "A distant aligned target must permit forward flight");
            }
            observed.serial = 0;
            observed.scene.entities[1].x = 0;
            observed.scene.entities[1].y = 0;
            flight.view.y = 0;
            flight.forward_speed = 0;
            require(braking.controls(flight, observed).vertical > 0,
                    "A serial reset and zero-range target must reset decisions and back away");
        }
        std::cout << "Keyboard pilot braking, pursuit, input edges and native-frame sampling passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
