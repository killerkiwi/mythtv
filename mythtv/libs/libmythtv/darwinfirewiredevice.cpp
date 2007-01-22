/**
 *  DarwinFirewireChannel
 *  Copyright (c) 2005 by Jim Westfall
 *  SA3250HD support Copyright (c) 2005 by Matt Porter
 *  Copyright (c) 2006 by Dave Abrahams
 *  Distributed as part of MythTV under GPL v2 and later.
 */

// POSIX headers
#include <pthread.h>

// OS X headers
#undef always_inline
#include <IOKit/IOMessage.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/firewire/IOFireWireLib.h>
#include <IOKit/firewire/IOFireWireLibIsoch.h>
#include <IOKit/firewire/IOFireWireFamilyCommon.h>
#include <IOKit/avc/IOFireWireAVCLib.h>

// Std C++ headers
#include <algorithm>
#include <vector>
using namespace std;

// MythTV headers
#include "darwinfirewiredevice.h"
#include "darwinavcinfo.h"
#include "mythcontext.h"

// Apple Firewire example headers
#include <AVCVideoServices/StringLogger.h>
#include <AVCVideoServices/MPEG2Receiver.h>

// header not used because it also requires MPEG2Transmitter.h
//#include <AVCVideoServices/FireWireMPEG.h>
namespace AVS
{
    IOReturn CreateMPEG2Receiver(
        MPEG2Receiver           **ppReceiver,
        DataPushProc              dataPushProcHandler,
        void                     *pDataPushProcRefCon = nil,
        MPEG2ReceiverMessageProc  messageProcHandler  = nil,
        void                     *pMessageProcRefCon  = nil,
        StringLogger             *stringLogger        = nil,
        IOFireWireLibNubRef       nubInterface        = nil,
        unsigned int              cyclesPerSegment    =
            kCyclesPerReceiveSegment,
        unsigned int              numSegments         =
            kNumReceiveSegments,
        bool                      doIRMAllocations    = false);
    IOReturn DestroyMPEG2Receiver(MPEG2Receiver *pReceiver);
}

#define LOC      QString("DFireDev(%1): ").arg(guid_to_string(m_guid))
#define LOC_WARN QString("DFireDev(%1), Warning: ").arg(guid_to_string(m_guid))
#define LOC_ERR  QString("DFireDev(%1), Error: ").arg(guid_to_string(m_guid))

#define kAnyAvailableIsochChannel 0xFFFFFFFF
#define kNoDataTimeout            300  /* msec */
#define kResetTimeout             1500 /* msec */

static IOReturn dfd_tspacket_handler_thunk(
    long unsigned int tsPacketCount, UInt32 **ppBuf, void *callback_data);
static void dfd_update_device_list(void *dfd, io_iterator_t iterator);
static void dfd_streaming_log_message(char *pString);

class DFDPriv
{
  public:
    DFDPriv() :
        controller_thread_cf_ref(NULL), controller_thread_running(false),
        notify_port(NULL), notify_source(NULL), deviter(NULL),
        actual_fwchan(-1), is_streaming(false), avstream(NULL), logger(NULL),
        no_data_cnt(0), no_data_timer_set(false)
    {
        logger = new AVS::StringLogger(dfd_streaming_log_message);
    }

    ~DFDPriv()
    {
        avcinfo_list_t::iterator it = devices.begin();
        for (; it != devices.end(); ++it)
            delete (*it);
        devices.clear();

        if (logger)
        {
            delete logger;
            logger = NULL;
        }
    }

    pthread_t                 controller_thread;
    CFRunLoopRef              controller_thread_cf_ref;
    bool                      controller_thread_running;

    IONotificationPortRef     notify_port;
    CFRunLoopSourceRef        notify_source;
    io_iterator_t             deviter;

    int                       actual_fwchan;
    bool                      is_streaming;
    AVS::MPEG2Receiver       *avstream;
    AVS::StringLogger        *logger;
    uint                      no_data_cnt;
    bool                      no_data_timer_set;
    MythTimer                 no_data_timer;

    avcinfo_list_t            devices;
};

DarwinFirewireDevice::DarwinFirewireDevice(
    uint64_t guid, uint subunitid, uint speed) :
    FirewireDevice(guid, subunitid, speed),
    m_local_node(-1), m_remote_node(-1), m_priv(new DFDPriv())
{


}

DarwinFirewireDevice::~DarwinFirewireDevice()
{
    if (IsPortOpen())
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR + "ctor called with open port");
        while (IsPortOpen())
            ClosePort();
    }

    if (m_priv)
    {
        delete m_priv;
        m_priv = NULL;
    }
}

void DarwinFirewireDevice::RunController(void)
{
    m_priv->controller_thread_cf_ref = CFRunLoopGetCurrent();

    // Set up IEEE-1394 bus change notification
    mach_port_t master_port;
    int ret = IOMasterPort(bootstrap_port, &master_port);
    if (kIOReturnSuccess == ret)
    {
        m_priv->notify_port   = IONotificationPortCreate(master_port);
        m_priv->notify_source = IONotificationPortGetRunLoopSource(
            m_priv->notify_port);

        CFRunLoopAddSource(m_priv->controller_thread_cf_ref,
                           m_priv->notify_source,
                           kCFRunLoopDefaultMode);

        ret = IOServiceAddMatchingNotification(
            m_priv->notify_port, kIOMatchedNotification,
            IOServiceMatching("IOFireWireAVCUnit"),
            dfd_update_device_list, this, &m_priv->deviter);
    }

    if (kIOReturnSuccess == ret)
        dfd_update_device_list(this, m_priv->deviter);

    m_priv->controller_thread_running = true;

    if (kIOReturnSuccess == ret)
        CFRunLoopRun();

    QMutexLocker locker(&m_lock); // ensure that controller_thread_running seen

    m_priv->controller_thread_running = false;
}

void DarwinFirewireDevice::StartController(void)
{
    m_lock.unlock();

    pthread_create(&m_priv->controller_thread, NULL,
                   dfd_controller_thunk, this);

    m_lock.lock();
    while (!m_priv->controller_thread_running)
    {
        m_lock.unlock();
        usleep(5000);
        m_lock.lock();
    }
}

void DarwinFirewireDevice::StopController(void)
{
    if (!m_priv->controller_thread_running)
        return;

    if (m_priv->deviter)
    {
        IOObjectRelease(m_priv->deviter);
        m_priv->deviter = NULL;
    }
    
    if (m_priv->notify_source)
    {
        CFRunLoopSourceInvalidate(m_priv->notify_source);
        m_priv->notify_source = NULL;
    }

    if (m_priv->notify_port)
    {
        IONotificationPortDestroy(m_priv->notify_port);
        m_priv->notify_port = NULL;
    }

    CFRunLoopStop(m_priv->controller_thread_cf_ref);
    
    while (m_priv->controller_thread_running)
    {
        m_lock.unlock();
        usleep(100 * 1000);
        m_lock.lock();
    }
}

bool DarwinFirewireDevice::OpenPort(void)
{
    QMutexLocker locker(&m_lock);

    VERBOSE(VB_RECORD, LOC + "OpenPort()");

    if (GetInfoPtr() && GetInfoPtr()->IsPortOpen())
    {
        m_open_port_cnt++;
        return true;
    }

    StartController();

    if (!m_priv->controller_thread_running)
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR + "Unable to start firewire thread.");
        return false;
    }

    if (!GetInfoPtr())
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR + "No IEEE-1394 device with our GUID");
        
        StopController();
        return false;
    }

    VERBOSE(VB_RECORD, LOC + "Opening AVC Device");
    VERBOSE(VB_RECORD, LOC + GetInfoPtr()->GetSubunitInfoString());

    if (!GetInfoPtr()->IsSubunitType(kAVCSubunitTypeTuner) ||
        !GetInfoPtr()->IsSubunitType(kAVCSubunitTypePanel))
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR + QString("No STB at guid: 0x%1")
                .arg(m_guid,0,16));

        StopController();
        return false;
    }

    bool ok = GetInfoPtr()->OpenPort(m_priv->controller_thread_cf_ref);
    if (!ok)
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR + "Unable to get handle for port");

        return false;
    }

    // TODO FIXME -- these can change after a reset... (at least local)
    if (!GetInfoPtr()->GetDeviceNodes(m_local_node, m_remote_node))
    {
        if (m_local_node < 0)
        {
            VERBOSE(VB_IMPORTANT, LOC_WARN + "Failed to query local node");
            m_local_node = 0;
        }

        if (m_remote_node < 0)
        {
            VERBOSE(VB_IMPORTANT, LOC_WARN + "Failed to query remote node");
            m_remote_node = 0;
        }
    }

    m_open_port_cnt++;

    return true;
}

bool DarwinFirewireDevice::ClosePort(void)
{
    QMutexLocker locker(&m_lock);

    VERBOSE(VB_RECORD, LOC + "ClosePort()");

    if (m_open_port_cnt < 1)
        return false;

    m_open_port_cnt--;

    if (m_open_port_cnt != 0)
        return true;

    if (GetInfoPtr() && GetInfoPtr()->IsPortOpen())
    {
        VERBOSE(VB_RECORD, LOC + "Closing AVC Device");

        GetInfoPtr()->ClosePort();
    }

    StopController();
    m_local_node  = -1;
    m_remote_node = -1;

    return true;
}

bool DarwinFirewireDevice::OpenAVStream(void)
{
    if (IsAVStreamOpen())
        return true;

    int max_speed = GetMaxSpeed();
    VERBOSE(VB_IMPORTANT, "Max Speed: "<<max_speed<<" Our speed: "<<m_speed);
    m_speed = min((uint)max_speed, m_speed);

    uint fwchan = 0;
    bool streaming = IsSTBStreaming(&fwchan);
    VERBOSE(VB_IMPORTANT, QString("STB is %1already streaming on fwchan: %2")
            .arg(streaming?"":"not ").arg(fwchan));

    // TODO we should use the stream if it already exists,
    //      this is especially true if it is a broadcast stream...

    int ret = AVS::CreateMPEG2Receiver(
        &m_priv->avstream,
        dfd_tspacket_handler_thunk, this,
        dfd_stream_msg, this,
        m_priv->logger /* StringLogger */,
        GetInfoPtr()->fw_handle,
        AVS::kCyclesPerReceiveSegment,
        AVS::kNumReceiveSegments,
        true /* p2p */);

    if (kIOReturnSuccess != ret)
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR + "Couldn't create A/V stream object");
        return false;
    }

    m_priv->avstream->registerNoDataNotificationCallback(
        dfd_no_data_notification, this, kNoDataTimeout);

    return true;
}

int DarwinFirewireDevice::GetMaxSpeed(void)
{
    IOFireWireLibDeviceRef fw_handle = GetInfoPtr()->fw_handle;

    if ((*fw_handle)->version < 4)
    {
        // Just get the STB's info & assume we can handle it
        io_object_t dev = (*fw_handle)->GetDevice(fw_handle);

        FWAddress addr(0xffff, 0xf0000900, m_remote_node);
        uint32_t val;
        int ret = (*fw_handle)->ReadQuadlet(
            fw_handle, dev, &addr, (UInt32*) &val, false, 0);

        return (ret == kIOReturnSuccess) ? (int)((val>>30) & 0x3) : -1;
    }

    uint32_t generation = 0;
    IOFWSpeed speed;
    int ret = (*fw_handle)->GetBusGeneration(fw_handle, (UInt32*)&generation);
    if (kIOReturnSuccess == ret)
    {
        ret = (*fw_handle)->GetSpeedBetweenNodes(
            fw_handle, generation, m_remote_node, m_local_node, &speed) ;
    }

    return (ret == kIOReturnSuccess) ? (int)speed : -1;
}

bool DarwinFirewireDevice::IsSTBStreaming(uint *fw_channel)
{
    IOFireWireLibDeviceRef fw_handle = GetInfoPtr()->fw_handle;
    io_object_t dev = (*fw_handle)->GetDevice(fw_handle);

    FWAddress addr(0xffff, 0xf0000904, m_remote_node);
    uint32_t val;
    int ret = (*fw_handle)->ReadQuadlet(
        fw_handle, dev, &addr, (UInt32*) &val, false, 0);

    if (ret != kIOReturnSuccess)
        return false;

    if (val & (kIOFWPCRBroadcast | kIOFWPCRP2PCount))
    {
        if (fw_channel)
            *fw_channel = (val & kIOFWPCRChannel) >> kIOFWPCRChannelPhase;

        return true;
    }

    return false;
}

bool DarwinFirewireDevice::CloseAVStream(void)
{
    if (!m_priv->avstream)
        return true;

    StopStreaming();

    VERBOSE(VB_RECORD, LOC + "Destroying A/V stream object");
    AVS::DestroyMPEG2Receiver(m_priv->avstream);
    m_priv->avstream = NULL;

    return true;
}

bool DarwinFirewireDevice::IsAVStreamOpen(void) const
{
    return m_priv->avstream;
}

bool DarwinFirewireDevice::ResetBus(void)
{
    VERBOSE(VB_IMPORTANT, LOC + "ResetBus() -- begin");

    if (!GetInfoPtr() || !GetInfoPtr()->fw_handle)
        return false;

    IOFireWireLibDeviceRef fw_handle = GetInfoPtr()->fw_handle;
    bool ok = (*fw_handle)->BusReset(fw_handle) == kIOReturnSuccess;

    if (!ok)
        VERBOSE(VB_IMPORTANT, LOC_ERR + "Bus Reset failed" + ENO);

    VERBOSE(VB_IMPORTANT, LOC + "ResetBus() -- end");

    return ok;
}

bool DarwinFirewireDevice::StartStreaming(void)
{
    if (m_priv->is_streaming)
        return m_priv->is_streaming;

    VERBOSE(VB_RECORD, LOC + "Starting A/V streaming");

    if (!IsAVStreamOpen() && !OpenAVStream())
    {
        VERBOSE(VB_IMPORTANT, LOC + "Starting A/V streaming: FAILED");
        return false;
    }

    m_priv->avstream->setReceiveIsochChannel(kAnyAvailableIsochChannel);
    m_priv->avstream->setReceiveIsochSpeed((IOFWSpeed) m_speed);
    int ret = m_priv->avstream->startReceive();

    m_priv->is_streaming = (kIOReturnSuccess == ret);

    VERBOSE(VB_IMPORTANT, LOC + "Starting A/V streaming: "
            <<((m_priv->is_streaming)?"success":"failure"));

    return m_priv->is_streaming;
}

bool DarwinFirewireDevice::StopStreaming(void)
{
    if (!m_priv->is_streaming)
        return true;

    VERBOSE(VB_RECORD, LOC + "Stopping A/V streaming");

    bool ok = (kIOReturnSuccess == m_priv->avstream->stopReceive());
    m_priv->is_streaming = !ok;

    if (!ok)
    {
        VERBOSE(VB_RECORD, LOC_ERR + "Failed to stop A/V streaming");
        return false;
    }

    VERBOSE(VB_RECORD, LOC + "Stopped A/V streaming");
    return true;
}

bool DarwinFirewireDevice::SendAVCCommand(const vector<uint8_t> &cmd,
                                          vector<uint8_t>       &result,
                                          int                   retry_cnt)
{
    return GetInfoPtr()->SendAVCCommand(cmd, result, retry_cnt);
}

bool DarwinFirewireDevice::IsPortOpen(void) const
{
    QMutexLocker locker(&m_lock);

    if (!GetInfoPtr())
        return false;

    return GetInfoPtr()->IsPortOpen();
}

void DarwinFirewireDevice::AddListener(TSDataListener *listener)
{
    QMutexLocker locker(&m_lock);

    FirewireDevice::AddListener(listener);

    if (!m_listeners.empty())
        StartStreaming();
}

void DarwinFirewireDevice::RemoveListener(TSDataListener *listener)
{
    QMutexLocker locker(&m_lock);

    FirewireDevice::RemoveListener(listener);

    if (m_priv->is_streaming && m_listeners.empty())
    {
        StopStreaming();
        CloseAVStream();
    }
}

void DarwinFirewireDevice::BroadcastToListeners(
    const unsigned char *data, uint dataSize)
{
    QMutexLocker locker(&m_lock);
    FirewireDevice::BroadcastToListeners(data, dataSize);
}

void DarwinFirewireDevice::ProcessNoDataMessage(void)
{
    if (m_priv->no_data_timer_set)
    {
        int short_interval = kNoDataTimeout + (kNoDataTimeout>>1);
        bool recent = m_priv->no_data_timer.elapsed() <= short_interval;
        m_priv->no_data_cnt = (recent) ? m_priv->no_data_cnt + 1 : 1;
    }
    m_priv->no_data_timer_set = true;
    m_priv->no_data_timer.start();

    VERBOSE(VB_IMPORTANT, LOC_WARN + QString("No Input in %1 msecs")
            .arg(m_priv->no_data_cnt * kNoDataTimeout));

    if (m_priv->no_data_cnt > (kResetTimeout / kNoDataTimeout))
    {
        m_priv->no_data_timer_set = false;
        m_priv->no_data_cnt = 0;
        ResetBus();
    }
}

void DarwinFirewireDevice::ProcessStreamingMessage(
    uint32_t msg, uint32_t param1, uint32_t param2)
{
    int plug_number = 0;

    if (AVS::kMpeg2ReceiverAllocateIsochPort == msg)
    {
        int speed = param1, fw_channel = param2;

        bool ok = UpdatePlugRegister(
            plug_number, fw_channel, speed, true, false);

        VERBOSE(VB_IMPORTANT, LOC + QString("AllocateIsochPort(%1,%2) %3")
                .arg(fw_channel).arg(speed).arg(((ok)?"ok":"error")));
    }
    else if (AVS::kMpeg2ReceiverReleaseIsochPort == msg)
    {
        int ret = UpdatePlugRegister(plug_number, -1, -1, false, true);

        VERBOSE(VB_IMPORTANT, LOC + "ReleaseIsochPort "
                <<((kIOReturnSuccess == ret)?"ok":"error"));
    }
    else if (AVS::kMpeg2ReceiverDCLOverrun == msg)
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR + "DCL Overrun");
    }
    else if (AVS::kMpeg2ReceiverReceivedBadPacket == msg)
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR + "Received Bad Packet");
    }
    else
    {
        VERBOSE(VB_GENERAL, LOC +
                QString("Streaming Message: %1").arg(msg));
    }
}

vector<AVCInfo> DarwinFirewireDevice::GetSTBList(void)
{
    vector<AVCInfo> list;

    {
        DarwinFirewireDevice dev(0,0,0);

        dev.m_lock.lock();
        dev.StartController();
        dev.m_lock.unlock();

        list = dev.GetSTBListPrivate();

        dev.m_lock.lock();
        dev.StopController();
        dev.m_lock.unlock();
    }

    return list;
}

vector<AVCInfo> DarwinFirewireDevice::GetSTBListPrivate(void)
{
    VERBOSE(VB_IMPORTANT, "GetSTBListPrivate -- begin");
    QMutexLocker locker(&m_lock);
    VERBOSE(VB_IMPORTANT, "GetSTBListPrivate -- got lock");

    vector<AVCInfo> list;

    avcinfo_list_t::iterator it = m_priv->devices.begin();
    for (; it != m_priv->devices.end(); ++it)
    {
        if ((*it)->IsSubunitType(kAVCSubunitTypeTuner) &&
            (*it)->IsSubunitType(kAVCSubunitTypePanel))
        {
            list.push_back(*(*it));
        }
    }

    VERBOSE(VB_IMPORTANT, "GetSTBListPrivate -- end");
    return list;
}

void DarwinFirewireDevice::UpdateDeviceListItem(uint64_t guid, void *pitem)
{
    QMutexLocker locker(&m_lock);

    avcinfo_list_t::iterator it = m_priv->devices.find(guid);

    if (it == m_priv->devices.end())
    {
        DarwinAVCInfo *ptr = new DarwinAVCInfo();

        VERBOSE(VB_IMPORTANT, "Adding   0x"<<hex<<guid<<dec);

        m_priv->devices[guid] = ptr;
        it = m_priv->devices.find(guid);
    }

    io_object_t &item = *((io_object_t*) pitem);
    if (it != m_priv->devices.end())
    {
        VERBOSE(VB_IMPORTANT, "Updating 0x"<<hex<<guid<<dec);
        (*it)->Update(guid, this, m_priv->notify_port,
                      m_priv->controller_thread_cf_ref, item);
    }
}

DarwinAVCInfo *DarwinFirewireDevice::GetInfoPtr(void)
{
    avcinfo_list_t::iterator it = m_priv->devices.find(m_guid);
    return (it == m_priv->devices.end()) ? NULL : *it;
}

const DarwinAVCInfo *DarwinFirewireDevice::GetInfoPtr(void) const
{
    avcinfo_list_t::iterator it = m_priv->devices.find(m_guid);
    return (it == m_priv->devices.end()) ? NULL : *it;
}


bool DarwinFirewireDevice::UpdatePlugRegisterPrivate(
    uint plug_number, int new_fw_chan, int new_speed,
    bool add_plug, bool remove_plug)
{
    if (!GetInfoPtr())
        return false;

    IOFireWireLibDeviceRef fw_handle = GetInfoPtr()->fw_handle;
    if (!fw_handle)
        return false;

    io_object_t dev = (*fw_handle)->GetDevice(fw_handle);

    // Read the register
    uint      low_addr = kPCRBaseAddress + 4 + (plug_number << 2);
    FWAddress addr(0xffff, low_addr, m_remote_node);
    uint32_t  old_plug_val;
    if (kIOReturnSuccess != (*fw_handle)->ReadQuadlet(
            fw_handle, dev, &addr, (UInt32*) &old_plug_val, false, 0))
    {
        return false;
    }

    int old_plug_cnt = (old_plug_val >> 24) & 0x3f;
    int old_fw_chan  = (old_plug_val >> 16) & 0x3f;
    int old_speed    = (old_plug_val >> 14) & 0x03;

    int new_plug_cnt = (int) old_plug_cnt;
    new_plug_cnt += ((add_plug) ? 1 : 0) - ((remove_plug) ? 1 : 0);
    if ((new_plug_cnt > 0x3f) || (new_plug_cnt < 0))
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR + "Invalid Plug Count "<<new_plug_cnt);

        return false;
    }

    new_fw_chan = (new_fw_chan >= 0) ? new_fw_chan : old_fw_chan;
    if (old_plug_cnt && (new_fw_chan != old_fw_chan))
    {
        VERBOSE(VB_IMPORTANT, LOC_WARN +
                "Ignoring FWChan change request, plug already open");

        new_fw_chan = old_fw_chan;
    }

    new_speed = (new_speed >= 0) ? new_speed : old_speed;
    if (old_plug_cnt && (new_speed != old_speed))
    {
        VERBOSE(VB_IMPORTANT, LOC_WARN +
                "Ignoring speed change request, plug already open");

        new_speed = old_speed;
    }

    uint32_t new_plug_val = old_plug_val;

    new_plug_val &= ~(0x3f<<24);
    new_plug_val &= (remove_plug) ? ~kIOFWPCRBroadcast : ~0x0;
    new_plug_val |= (new_plug_cnt & 0x3f) << 24;

    new_plug_val &= ~(0x3f<<16);
    new_plug_val |= (new_fw_chan & 0x3F) << 16;

    new_plug_val &= ~(0x03<<14);
    new_plug_val |= (new_speed & 0x03) << 14;

    return (kIOReturnSuccess == (*fw_handle)->CompareSwap(
                fw_handle, dev, &addr, old_plug_val, new_plug_val, false, 0));
}

void DarwinFirewireDevice::HandleBusReset(void)
{
    int plug_number = 0;
    if (!GetInfoPtr())
        return;

    int fw_channel = m_priv->actual_fwchan;
    bool ok = UpdatePlugRegister(plug_number, fw_channel,
                                 m_speed, true, false);
    if (!ok)
    {
        ok = UpdatePlugRegister(plug_number, kAnyAvailableIsochChannel,
                                m_speed, true, false);
    }

    if (!ok)
        VERBOSE(VB_IMPORTANT, LOC + "Reset: Failed to reconnect");
    else
        VERBOSE(VB_RECORD, LOC + "Reset: Reconnected succesfully");
}

bool DarwinFirewireDevice::UpdatePlugRegister(
    uint plug_number, int fw_chan, int speed,
    bool add_plug, bool remove_plug, uint retry_cnt)
{
    if (!GetInfoPtr() || !GetInfoPtr()->fw_handle)
        return false;

    bool ok = false;

    for (uint i = 0; (i < retry_cnt) && !ok; i++)
    {
        ok = UpdatePlugRegisterPrivate(
            plug_number, fw_chan, speed, add_plug, remove_plug);
    }

    m_priv->actual_fwchan = (ok) ? fw_chan : kAnyAvailableIsochChannel;

    return ok;
}

void DarwinFirewireDevice::HandleDeviceChange(uint messageType)
{
    QString loc = LOC + "HandleDeviceChange: ";

    if (kIOMessageServiceIsTerminated == messageType)
    {
        VERBOSE(VB_RECORD, loc + "Disconnect");
        // stop printing no data messages.. don't try to open
        return;
    }

    if (kIOMessageServiceIsAttemptingOpen == messageType)
    {
        VERBOSE(VB_RECORD, loc + "Attempting open");
        return;
    }

    if (kIOMessageServiceWasClosed == messageType)
    {
        VERBOSE(VB_RECORD, loc + "Device Closed");
        // fill unit_table
        return;
    }

    if (kIOMessageServiceIsSuspended == messageType)
    {
        VERBOSE(VB_RECORD, loc + "kIOMessageServiceIsSuspended");
        // start of reset
        return;
    }

    if (kIOMessageServiceIsResumed == messageType)
    {
        // end of reset
        HandleBusReset();
    }

    if (kIOMessageServiceIsTerminated == messageType)
        VERBOSE(VB_RECORD, loc + "kIOMessageServiceIsTerminated");
    else if (kIOMessageServiceIsRequestingClose == messageType)
        VERBOSE(VB_RECORD, loc + "kIOMessageServiceIsRequestingClose");
    else if (kIOMessageServiceIsAttemptingOpen == messageType)
        VERBOSE(VB_RECORD, loc + "kIOMessageServiceIsAttemptingOpen");
    else if (kIOMessageServiceWasClosed == messageType)
        VERBOSE(VB_RECORD, loc + "kIOMessageServiceWasClosed");
    else if (kIOMessageServiceBusyStateChange == messageType)
        VERBOSE(VB_RECORD, loc + "kIOMessageServiceBusyStateChange");
    else if (kIOMessageCanDevicePowerOff == messageType)
        VERBOSE(VB_RECORD, loc + "kIOMessageCanDevicePowerOff");
    else if (kIOMessageDeviceWillPowerOff == messageType)
        VERBOSE(VB_RECORD, loc + "kIOMessageDeviceWillPowerOff");
    else if (kIOMessageDeviceWillNotPowerOff == messageType)
        VERBOSE(VB_RECORD, loc + "kIOMessageDeviceWillNotPowerOff");
    else if (kIOMessageDeviceHasPoweredOn == messageType)
        VERBOSE(VB_RECORD, loc + "kIOMessageDeviceHasPoweredOn");
    else if (kIOMessageCanSystemPowerOff == messageType)
        VERBOSE(VB_RECORD, loc + "kIOMessageCanSystemPowerOff");
    else if (kIOMessageSystemWillPowerOff == messageType)
        VERBOSE(VB_RECORD, loc + "kIOMessageSystemWillPowerOff");
    else if (kIOMessageSystemWillNotPowerOff == messageType)
        VERBOSE(VB_RECORD, loc + "kIOMessageSystemWillNotPowerOff");
    else if (kIOMessageCanSystemSleep == messageType)
        VERBOSE(VB_RECORD, loc + "kIOMessageCanSystemSleep");
    else if (kIOMessageSystemWillSleep == messageType)
        VERBOSE(VB_RECORD, loc + "kIOMessageSystemWillSleep");
    else if (kIOMessageSystemWillNotSleep == messageType)
        VERBOSE(VB_RECORD, loc + "kIOMessageSystemWillNotSleep");
    else if (kIOMessageSystemHasPoweredOn == messageType)
        VERBOSE(VB_RECORD, loc + "kIOMessageSystemHasPoweredOn");
    else if (kIOMessageSystemWillRestart == messageType)
        VERBOSE(VB_RECORD, loc + "kIOMessageSystemWillRestart");
    else
    {
        VERBOSE(VB_RECORD, loc + "unknown message 0x"
                <<hex<<messageType<<dec);
    }
}

// Various message callbacks.

void *dfd_controller_thunk(void *param)
{
    ((DarwinFirewireDevice*)param)->RunController();
    return NULL;
}

void dfd_update_device_list_item(
    DarwinFirewireDevice *dev, uint64_t guid, void *item)
{
    dev->UpdateDeviceListItem(guid, item);
}

int dfd_no_data_notification(void *callback_data)
{
    ((DarwinFirewireDevice*)callback_data)->ProcessNoDataMessage();

    return kIOReturnSuccess;
}

void dfd_stream_msg(long unsigned int msg, long unsigned int param1,
                    long unsigned int param2, void *callback_data)
{
    ((DarwinFirewireDevice*)callback_data)->
        ProcessStreamingMessage(msg, param1, param2);
}

int dfd_tspacket_handler(uint tsPacketCount, uint32_t **ppBuf,
                         void *callback_data)
{
    DarwinFirewireDevice *fw = (DarwinFirewireDevice*) callback_data;
    if (!fw)
        return kIOReturnBadArgument;

    for (uint32_t i = 0; i < tsPacketCount; ++i)
        fw->BroadcastToListeners((const unsigned char*) ppBuf[i], 188);

    return kIOReturnSuccess;
}

static IOReturn dfd_tspacket_handler_thunk(
    long unsigned int tsPacketCount, UInt32 **ppBuf, void *callback_data)
{
    return dfd_tspacket_handler(
        tsPacketCount, (uint32_t**)ppBuf, callback_data);
}

static void dfd_update_device_list(void *dfd, io_iterator_t deviter)
{
    DarwinFirewireDevice *dev = (DarwinFirewireDevice*) dfd;

    io_object_t it = NULL;
    while ((it = IOIteratorNext(deviter)))
    {
        uint64_t guid = 0;

        CFMutableDictionaryRef props;
        int ret = IORegistryEntryCreateCFProperties(
            it, &props, kCFAllocatorDefault, kNilOptions);

        if (kIOReturnSuccess == ret)
        {
            CFNumberRef GUIDDesc = (CFNumberRef)
                CFDictionaryGetValue(props, CFSTR("GUID"));
            CFNumberGetValue(GUIDDesc, kCFNumberSInt64Type, &guid);
            CFRelease(props);
            dfd_update_device_list_item(dev, guid, &it);
        }
    }
}

static void dfd_streaming_log_message(char *msg)
{
    VERBOSE(VB_RECORD, QString("MPEG2Receiver: %1").arg(msg));
}
