/**
 *
 *    Copyright (c) 2021 Project CHIP Authors
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#include "app/server/Server.h"
#include <app-common/zap-generated/attributes/Accessors.h>
#include <app-common/zap-generated/cluster-objects.h>
#include <app-common/zap-generated/ids/Attributes.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app/AttributeAccessInterface.h>
#include <app/EventLogging.h>
#include <app/reporting/reporting.h>
#include <app/util/attribute-storage.h>
#include <platform/ConnectivityManager.h>
#include <platform/DiagnosticDataProvider.h>

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;
using namespace chip::app::Clusters::GeneralDiagnostics;
using namespace chip::app::Clusters::GeneralDiagnostics::Attributes;
using namespace chip::DeviceLayer;
using chip::DeviceLayer::ConnectivityMgr;
using chip::DeviceLayer::DiagnosticDataProvider;
using chip::DeviceLayer::GetDiagnosticDataProvider;
using chip::Protocols::InteractionModel::Status;

namespace {

bool IsTestEventTriggerEnabled()
{
    auto * triggerDelegate = Server::GetInstance().GetTestEventTriggerDelegate();
    if (triggerDelegate == nullptr)
    {
        return false;
    }
    uint8_t zeroByteSpanData[TestEventTriggerDelegate::kEnableKeyLength] = { 0 };
    if (triggerDelegate->DoesEnableKeyMatch(ByteSpan(zeroByteSpanData)))
    {
        return false;
    }
    return true;
}

bool IsByteSpanAllZeros(const ByteSpan & byteSpan)
{
    for (auto * it = byteSpan.begin(); it != byteSpan.end(); ++it)
    {
        if (*it != 0)
        {
            return false;
        }
    }
    return true;
}

class GeneralDiagosticsAttrAccess : public AttributeAccessInterface
{
public:
    // Register for the GeneralDiagnostics cluster on all endpoints.
    GeneralDiagosticsAttrAccess() : AttributeAccessInterface(Optional<EndpointId>::Missing(), GeneralDiagnostics::Id) {}

    CHIP_ERROR Read(const ConcreteReadAttributePath & aPath, AttributeValueEncoder & aEncoder) override;

private:
    template <typename T>
    CHIP_ERROR ReadIfSupported(CHIP_ERROR (DiagnosticDataProvider::*getter)(T &), AttributeValueEncoder & aEncoder);

    template <typename T>
    CHIP_ERROR ReadListIfSupported(CHIP_ERROR (DiagnosticDataProvider::*getter)(T &), AttributeValueEncoder & aEncoder);

    CHIP_ERROR ReadNetworkInterfaces(AttributeValueEncoder & aEncoder);
};

template <typename T>
CHIP_ERROR GeneralDiagosticsAttrAccess::ReadIfSupported(CHIP_ERROR (DiagnosticDataProvider::*getter)(T &),
                                                        AttributeValueEncoder & aEncoder)
{
    T data;
    CHIP_ERROR err = (GetDiagnosticDataProvider().*getter)(data);
    if (err == CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE)
    {
        data = {};
    }
    else if (err != CHIP_NO_ERROR)
    {
        return err;
    }

    return aEncoder.Encode(data);
}

template <typename T>
CHIP_ERROR GeneralDiagosticsAttrAccess::ReadListIfSupported(CHIP_ERROR (DiagnosticDataProvider::*getter)(T &),
                                                            AttributeValueEncoder & aEncoder)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    T faultList;

    if ((GetDiagnosticDataProvider().*getter)(faultList) == CHIP_NO_ERROR)
    {
        err = aEncoder.EncodeList([&faultList](const auto & encoder) -> CHIP_ERROR {
            for (auto fault : faultList)
            {
                ReturnErrorOnFailure(encoder.Encode(fault));
            }

            return CHIP_NO_ERROR;
        });
    }
    else
    {
        err = aEncoder.EncodeEmptyList();
    }

    return err;
}

CHIP_ERROR GeneralDiagosticsAttrAccess::ReadNetworkInterfaces(AttributeValueEncoder & aEncoder)
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    DeviceLayer::NetworkInterface * netifs;

    if (DeviceLayer::GetDiagnosticDataProvider().GetNetworkInterfaces(&netifs) == CHIP_NO_ERROR)
    {
        err = aEncoder.EncodeList([&netifs](const auto & encoder) -> CHIP_ERROR {
            for (DeviceLayer::NetworkInterface * ifp = netifs; ifp != nullptr; ifp = ifp->Next)
            {
                ReturnErrorOnFailure(encoder.Encode(*ifp));
            }

            return CHIP_NO_ERROR;
        });

        DeviceLayer::GetDiagnosticDataProvider().ReleaseNetworkInterfaces(netifs);
    }
    else
    {
        err = aEncoder.EncodeEmptyList();
    }

    return err;
}

GeneralDiagosticsAttrAccess gAttrAccess;

CHIP_ERROR GeneralDiagosticsAttrAccess::Read(const ConcreteReadAttributePath & aPath, AttributeValueEncoder & aEncoder)
{
    if (aPath.mClusterId != GeneralDiagnostics::Id)
    {
        // We shouldn't have been called at all.
        return CHIP_ERROR_INVALID_ARGUMENT;
    }

    switch (aPath.mAttributeId)
    {
    case NetworkInterfaces::Id: {
        return ReadNetworkInterfaces(aEncoder);
    }
    case ActiveHardwareFaults::Id: {
        return ReadListIfSupported(&DiagnosticDataProvider::GetActiveHardwareFaults, aEncoder);
    }
    case ActiveRadioFaults::Id: {
        return ReadListIfSupported(&DiagnosticDataProvider::GetActiveRadioFaults, aEncoder);
    }
    case ActiveNetworkFaults::Id: {
        return ReadListIfSupported(&DiagnosticDataProvider::GetActiveNetworkFaults, aEncoder);
    }
    case RebootCount::Id: {
        return ReadIfSupported(&DiagnosticDataProvider::GetRebootCount, aEncoder);
    }
    case UpTime::Id: {
        return ReadIfSupported(&DiagnosticDataProvider::GetUpTime, aEncoder);
    }
    case TotalOperationalHours::Id: {
        return ReadIfSupported(&DiagnosticDataProvider::GetTotalOperationalHours, aEncoder);
    }
    case BootReasons::Id: {
        return ReadIfSupported(&DiagnosticDataProvider::GetBootReason, aEncoder);
    }
    case TestEventTriggersEnabled::Id: {
        bool isTestEventTriggersEnabled = IsTestEventTriggerEnabled();
        return aEncoder.Encode(isTestEventTriggersEnabled);
    }
    default: {
        break;
    }
    }
    return CHIP_NO_ERROR;
}

class GeneralDiagnosticsDelegate : public DeviceLayer::ConnectivityManagerDelegate, public DeviceLayer::GeneralDiagnosticsDelegate
{
    static void ReportAttributeOnAllEndpoints(AttributeId attribute)
    {
        for (auto endpoint : EnabledEndpointsWithServerCluster(GeneralDiagnostics::Id))
        {
            MatterReportingAttributeChangeCallback(endpoint, GeneralDiagnostics::Id, attribute);
        }
    }

    // Gets called when any network interface on the Node is updated.
    void OnNetworkInfoChanged() override
    {
        ChipLogDetail(Zcl, "GeneralDiagnosticsDelegate: OnNetworkInfoChanged");

        ReportAttributeOnAllEndpoints(GeneralDiagnostics::Attributes::NetworkInterfaces::Id);
    }

    // Gets called when the device has been rebooted.
    void OnDeviceRebooted(BootReasonType bootReason) override
    {
        ChipLogDetail(Zcl, "GeneralDiagnosticsDelegate: OnDeviceRebooted");

        ReportAttributeOnAllEndpoints(GeneralDiagnostics::Attributes::BootReasons::Id);

        // GeneralDiagnostics cluster should exist only for endpoint 0.

        Events::BootReason::Type event{ bootReason };
        EventNumber eventNumber;

        CHIP_ERROR err = LogEvent(event, 0, eventNumber);
        if (CHIP_NO_ERROR != err)
        {
            ChipLogError(Zcl, "GeneralDiagnosticsDelegate: Failed to record BootReason event: %" CHIP_ERROR_FORMAT, err.Format());
        }
    }

    // Get called when the Node detects a hardware fault has been raised.
    void OnHardwareFaultsDetected(GeneralFaults<kMaxHardwareFaults> & previous,
                                  GeneralFaults<kMaxHardwareFaults> & current) override
    {
        ChipLogDetail(Zcl, "GeneralDiagnosticsDelegate: OnHardwareFaultsDetected");

        for (auto endpointId : EnabledEndpointsWithServerCluster(GeneralDiagnostics::Id))
        {
            // If General Diagnostics cluster is implemented on this endpoint
            MatterReportingAttributeChangeCallback(endpointId, GeneralDiagnostics::Id,
                                                   GeneralDiagnostics::Attributes::ActiveHardwareFaults::Id);

            // Record HardwareFault event
            EventNumber eventNumber;
            DataModel::List<const HardwareFaultType> currentList = DataModel::List<const HardwareFaultType>(
                reinterpret_cast<const HardwareFaultType *>(current.data()), current.size());
            DataModel::List<const HardwareFaultType> previousList = DataModel::List<const HardwareFaultType>(
                reinterpret_cast<const HardwareFaultType *>(previous.data()), previous.size());
            Events::HardwareFaultChange::Type event{ currentList, previousList };

            if (CHIP_NO_ERROR != LogEvent(event, endpointId, eventNumber))
            {
                ChipLogError(Zcl, "GeneralDiagnosticsDelegate: Failed to record HardwareFault event");
            }
        }
    }

    // Get called when the Node detects a radio fault has been raised.
    void OnRadioFaultsDetected(GeneralFaults<kMaxRadioFaults> & previous, GeneralFaults<kMaxRadioFaults> & current) override
    {
        ChipLogDetail(Zcl, "GeneralDiagnosticsDelegate: OnRadioFaultsDetected");

        for (auto endpointId : EnabledEndpointsWithServerCluster(GeneralDiagnostics::Id))
        {
            // If General Diagnostics cluster is implemented on this endpoint
            MatterReportingAttributeChangeCallback(endpointId, GeneralDiagnostics::Id,
                                                   GeneralDiagnostics::Attributes::ActiveRadioFaults::Id);

            // Record RadioFault event
            EventNumber eventNumber;
            DataModel::List<const RadioFaultType> currentList =
                DataModel::List<const RadioFaultType>(reinterpret_cast<const RadioFaultType *>(current.data()), current.size());
            DataModel::List<const RadioFaultType> previousList =
                DataModel::List<const RadioFaultType>(reinterpret_cast<const RadioFaultType *>(previous.data()), previous.size());
            Events::RadioFaultChange::Type event{ currentList, previousList };

            if (CHIP_NO_ERROR != LogEvent(event, endpointId, eventNumber))
            {
                ChipLogError(Zcl, "GeneralDiagnosticsDelegate: Failed to record RadioFault event");
            }
        }
    }

    // Get called when the Node detects a network fault has been raised.
    void OnNetworkFaultsDetected(GeneralFaults<kMaxNetworkFaults> & previous, GeneralFaults<kMaxNetworkFaults> & current) override
    {
        ChipLogDetail(Zcl, "GeneralDiagnosticsDelegate: OnNetworkFaultsDetected");

        for (auto endpointId : EnabledEndpointsWithServerCluster(GeneralDiagnostics::Id))
        {
            // If General Diagnostics cluster is implemented on this endpoint
            MatterReportingAttributeChangeCallback(endpointId, GeneralDiagnostics::Id,
                                                   GeneralDiagnostics::Attributes::ActiveNetworkFaults::Id);

            // Record NetworkFault event
            EventNumber eventNumber;
            DataModel::List<const NetworkFaultType> currentList =
                DataModel::List<const NetworkFaultType>(reinterpret_cast<const NetworkFaultType *>(current.data()), current.size());
            DataModel::List<const NetworkFaultType> previousList = DataModel::List<const NetworkFaultType>(
                reinterpret_cast<const NetworkFaultType *>(previous.data()), previous.size());
            Events::NetworkFaultChange::Type event{ currentList, previousList };

            if (CHIP_NO_ERROR != LogEvent(event, endpointId, eventNumber))
            {
                ChipLogError(Zcl, "GeneralDiagnosticsDelegate: Failed to record NetworkFault event");
            }
        }
    }
};

GeneralDiagnosticsDelegate gDiagnosticDelegate;

} // anonymous namespace

bool emberAfGeneralDiagnosticsClusterTestEventTriggerCallback(CommandHandler * commandObj, const ConcreteCommandPath & commandPath,
                                                              const Commands::TestEventTrigger::DecodableType & commandData)
{

    if (commandData.enableKey.size() != TestEventTriggerDelegate::kEnableKeyLength)
    {
        commandObj->AddStatus(commandPath, Status::ConstraintError);
        return true;
    }

    if (IsByteSpanAllZeros(commandData.enableKey))
    {
        commandObj->AddStatus(commandPath, Status::ConstraintError);
        return true;
    }

    auto * triggerDelegate = Server::GetInstance().GetTestEventTriggerDelegate();

    if (triggerDelegate == nullptr || !triggerDelegate->DoesEnableKeyMatch(commandData.enableKey))
    {
        commandObj->AddStatus(commandPath, Status::UnsupportedAccess);
        return true;
    }

    CHIP_ERROR handleEventTriggerResult = triggerDelegate->HandleEventTrigger(commandData.eventTrigger);
    Status returnStatus                 = StatusIB(handleEventTriggerResult).mStatus;

    // When HandleEventTrigger returns INVALID_ARGUMENT we convert that into InvalidCommand to be spec
    // compliant.
    if (handleEventTriggerResult == CHIP_ERROR_INVALID_ARGUMENT)
    {
        returnStatus = Status::InvalidCommand;
    }

    commandObj->AddStatus(commandPath, returnStatus);
    return true;
}

void MatterGeneralDiagnosticsPluginServerInitCallback()
{
    registerAttributeAccessOverride(&gAttrAccess);

    ConnectivityMgr().SetDelegate(&gDiagnosticDelegate);
    GetDiagnosticDataProvider().SetGeneralDiagnosticsDelegate(&gDiagnosticDelegate);
}
