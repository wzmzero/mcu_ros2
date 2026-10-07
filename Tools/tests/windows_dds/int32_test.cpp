#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/SampleInfo.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>
#include <fastdds/rtps/common/SerializedPayload.h>
#include <fastdds/rtps/common/Locator.h>
#include <fastdds/rtps/transport/UDPv4TransportDescriptor.h>
#include <fastrtps/utils/IPLocator.h>
#include <fastcdr/Cdr.h>
#include <fastcdr/FastBuffer.h>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <set>
#include <thread>

using namespace eprosima::fastdds::dds;
using eprosima::fastrtps::rtps::SerializedPayload_t;
using eprosima::fastrtps::rtps::InstanceHandle_t;

// ROS std_msgs/msg/Int32: XCDR1 encapsulation followed by one signed int32.
class Int32Type final : public TopicDataType {
public:
    Int32Type() { setName("std_msgs::msg::dds_::Int32_"); m_typeSize = 8; m_isGetKeyDefined = false; }
    bool serialize(void* data, SerializedPayload_t* payload) override {
        try {
            eprosima::fastcdr::FastBuffer buffer(reinterpret_cast<char*>(payload->data), payload->max_size);
            eprosima::fastcdr::Cdr cdr(buffer, eprosima::fastcdr::Cdr::DEFAULT_ENDIAN,
                                      eprosima::fastcdr::CdrVersion::XCDRv1);
            payload->encapsulation = cdr.endianness() == eprosima::fastcdr::Cdr::BIG_ENDIANNESS ? CDR_BE : CDR_LE;
            cdr.serialize_encapsulation();
            cdr << *static_cast<int32_t*>(data);
            payload->length = static_cast<uint32_t>(cdr.get_serialized_data_length());
            return true;
        } catch (...) { return false; }
    }
    bool deserialize(SerializedPayload_t* payload, void* data) override {
        try {
            eprosima::fastcdr::FastBuffer buffer(reinterpret_cast<char*>(payload->data), payload->length);
            eprosima::fastcdr::Cdr cdr(buffer);
            cdr.read_encapsulation();
            cdr >> *static_cast<int32_t*>(data);
            return true;
        } catch (...) { return false; }
    }
    std::function<uint32_t()> getSerializedSizeProvider(void*) override { return [] { return 8u; }; }
    void* createData() override { return new int32_t(0); }
    void deleteData(void* data) override { delete static_cast<int32_t*>(data); }
    bool getKey(void*, InstanceHandle_t*, bool) override { return false; }
};

int main(int argc, char** argv) {
    const int domain = argc > 1 ? std::atoi(argv[1]) : 0;
    if (domain < 0 || domain > 232) return 2;
    auto* factory = DomainParticipantFactory::get_instance();
    DomainParticipantQos qos;
    qos.name("micro_ros_windows_data_test");
    qos.transport().use_builtin_transports = false;
    qos.transport().user_transports.push_back(std::make_shared<eprosima::fastdds::rtps::UDPv4TransportDescriptor>());
    // Probe native Agent participants over localhost as well as normal DDS
    // discovery. No WSL interface or cross-host DDS traffic is needed.
    for (int id = 0; id < 8; ++id) {
        eprosima::fastrtps::rtps::Locator_t peer;
        peer.kind = LOCATOR_KIND_UDPv4;
        eprosima::fastrtps::rtps::IPLocator::setIPv4(peer, 127, 0, 0, 1);
        peer.port = static_cast<uint32_t>(7400 + domain * 250 + 10 + id * 2);
        qos.wire_protocol().builtin.initialPeersList.push_back(peer);
    }
    auto* participant = factory->create_participant(static_cast<uint32_t>(domain), qos);
    if (!participant) { std::cerr << "Cannot create DDS participant\n"; return 2; }
    TypeSupport type(new Int32Type());
    type.register_type(participant);
    auto* heartbeat = participant->create_topic("rt/esp32s3/heartbeat", type.get_type_name(), TOPIC_QOS_DEFAULT);
    auto* echo = participant->create_topic("rt/esp32s3/echo", type.get_type_name(), TOPIC_QOS_DEFAULT);
    auto* command = participant->create_topic("rt/esp32s3/command", type.get_type_name(), TOPIC_QOS_DEFAULT);
    auto* publisher = participant->create_publisher(PUBLISHER_QOS_DEFAULT);
    auto* subscriber = participant->create_subscriber(SUBSCRIBER_QOS_DEFAULT);
    if (!heartbeat || !echo || !command || !publisher || !subscriber) return 2;
    DataReaderQos reader_qos;
    reader_qos.reliability().kind = RELIABLE_RELIABILITY_QOS;
    reader_qos.history().depth = 10;
    DataWriterQos writer_qos;
    writer_qos.reliability().kind = RELIABLE_RELIABILITY_QOS;
    writer_qos.history().depth = 10;
    auto* heartbeat_reader = subscriber->create_datareader(heartbeat, reader_qos);
    auto* echo_reader = subscriber->create_datareader(echo, reader_qos);
    auto* command_writer = publisher->create_datawriter(command, writer_qos);
    if (!heartbeat_reader || !echo_reader || !command_writer) return 2;
    int32_t sent = 57007;
    std::set<int32_t> received_heartbeats;
    int echoes = 0;
    SampleInfo info;
    int32_t received = 0;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (std::chrono::steady_clock::now() < deadline) {
        command_writer->write(&sent);
        while (heartbeat_reader->take_next_sample(&received, &info) == ReturnCode_t::RETCODE_OK) {
            if (info.valid_data) { received_heartbeats.insert(received); std::cout << "heartbeat=" << received << std::endl; }
        }
        while (echo_reader->take_next_sample(&received, &info) == ReturnCode_t::RETCODE_OK) {
            if (info.valid_data && received == sent) { ++echoes; std::cout << "echo=" << received << std::endl; }
        }
        if (received_heartbeats.size() >= 3 && echoes >= 3) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    const bool passed = received_heartbeats.size() >= 3 && echoes >= 3;
    std::cout << "RESULT heartbeats=" << received_heartbeats.size() << " echoes=" << echoes
              << " passed=" << (passed ? "true" : "false") << std::endl;
    participant->delete_contained_entities();
    factory->delete_participant(participant);
    return passed ? 0 : 1;
}
