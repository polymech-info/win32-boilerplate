#include "features.h"

#ifdef ENABLE_RS485

#include <Logger.h>
#include <components/RS485.h>
#include <components/OmronE5.h>
#include <components/SAKO_VFD.h>
#include <components/Delta_VFD.h>
#include <components/Loadcell.h>
#include "RS485Devices.h"
#include "PHApp.h"

void RS485Devices::registerApplicationDevices(RS485 *rs485)
{
    if (rs485 == nullptr)
    {
        LS_ERROR(F("RS485Devices: Cannot register devices, RS485 interface is null!"));
        return;
    }
    LS_INFO(F("RS485Devices: Registering %d application RS485 slaves..."), NUM_OMRON_DEVICES);
    PHApp *phApp = (PHApp *)rs485->owner;

#ifdef ENABLE_OMRON_E5
    for (uint8_t i = 0; i < NUM_OMRON_DEVICES; ++i)
    {
        uint8_t omronSlaveId = OMRON_E5_SLAVE_ID_BASE + i;
        OmronE5 *omronDevice = new OmronE5(rs485, omronSlaveId, phApp->appSettings->get("OMRON_E5_READ_BLOCK_INTERVAL", (uint32_t)OMRON_E5_READ_BLOCK_INTERVAL));
        omronDevice->setup();
        if (!rs485->deviceManager.addDevice(omronDevice))
        {
            LS_ERROR(F("RS485Devices: Failed to add OmronE5 Slave %d to manager"), omronSlaveId);
            delete omronDevice;
        }else{
            //LS_INFO("RS485Devices: Added OmronE5 Slave %d to manager", omronSlaveId);
        }
    }
#endif
#ifdef ENABLE_LOADCELL_0
    if (phApp->loadCell_0)
    {
        static_cast<RTU_Base*>(phApp->loadCell_0)->owner = rs485;
        if (!rs485->deviceManager.addDevice(phApp->loadCell_0))
        {
            LS_ERROR(F("RS485Devices: Failed to add Loadcell to manager"));
        }
    }
#endif
#ifdef ENABLE_LOADCELL_1
    if (phApp->loadCell_1)
    {
        static_cast<RTU_Base*>(phApp->loadCell_1)->owner = rs485;
        if (!rs485->deviceManager.addDevice(phApp->loadCell_1))
        {
            LS_ERROR(F("RS485Devices: Failed to add Loadcell to manager"));
        }
    }
#endif

#ifdef ENABLE_SAKO_VFD
    phApp->vfd_0->owner = rs485;
    if (!rs485->deviceManager.addDevice(phApp->vfd_0))
    {
        LS_ERROR(F("RS485Devices: Failed to add SAKO_VFD Slave %d to manager"), MB_SAKO_VFD_SLAVE_ID);
    }
#endif

#ifdef ENABLE_DELTA_VFD
    phApp->vfd_2->owner = rs485;
    if (!rs485->deviceManager.addDevice(phApp->vfd_2))
    {
        LS_ERROR(F("RS485Devices: Failed to add DELTA_VFD Slave %d to manager"), MB_DELTA_VFD_SLAVE_ID);
    }
#endif

    LS_INFO(F("RS485Devices: Finished registering application RS485 slaves."));
}

#endif