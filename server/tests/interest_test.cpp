#include "antor/zone/interest.hpp"
#include "test_harness.hpp"

#include <chrono>

void test_interest() {
    antor::zone::Zone zone(3, 8);
    EXPECT(zone.id() == 3);
    EXPECT(zone.shard_id() == 8);
    zone.add_session(10);
    zone.add_session(10);
    zone.add_session(11);
    zone.add_session(12);
    EXPECT(zone.sessions().size() == 3);

    antor::zone::InterestManager interest;
    const auto recipients = interest.recipients(zone);
    EXPECT(recipients.size() == 3);
    EXPECT(recipients[0] == 10);
    EXPECT(recipients[1] == 11);
    EXPECT(recipients[2] == 12);

    zone.remove_session(11);
    EXPECT(!zone.contains(11));
    EXPECT(interest.recipients(zone).size() == 2);

    zone.tick(std::chrono::milliseconds(50));
    zone.tick(std::chrono::milliseconds(50));
    EXPECT(zone.ticks() == 2);
    EXPECT(zone.last_dt() == std::chrono::milliseconds(50));
}
