#ifndef VIDEOSOURCE_H
#define VIDEOSOURCE_H

#include "libmyth/settings.h"
#include <qregexp.h>
#include <vector>
#include <qdir.h>
#include <qstringlist.h>

#include "channelsettings.h"

#ifdef USING_DVB
#include "dvbchannel.h"
#endif

class VideoSource;
class VSSetting: public SimpleDBStorage {
protected:
    VSSetting(const VideoSource& _parent, QString name):
        SimpleDBStorage("videosource", name),
        parent(_parent) {
        setName(name);
    };

    virtual QString setClause(void);
    virtual QString whereClause(void);

    const VideoSource& parent;
};


class XMLTVGrabber: public ComboBoxSetting, public VSSetting {
public:
    XMLTVGrabber(const VideoSource& parent): VSSetting(parent, "xmltvgrabber") {
        setLabel(QObject::tr("XMLTV listings grabber"));
    };
};

class PostalCode: public LineEditSetting, public TransientStorage {
public: 
    PostalCode() { 
        setLabel(QObject::tr("ZIP/postal code")); 
    };
};

class RegionSelector: public ComboBoxSetting, public TransientStorage {
    Q_OBJECT
public:
    RegionSelector() {
        setLabel(QObject::tr("Region"));
        fillSelections();
    };

public slots:
    void fillSelections();
};

class ProviderSelector: public ComboBoxSetting, public TransientStorage {
    Q_OBJECT
public:
    ProviderSelector(const QString& _grabber) :
        grabber(_grabber) { setLabel(QObject::tr("Provider")); };

public slots:
    void fillSelections(const QString& location);

protected:
    QString grabber;
};

class FreqTableSelector: public ComboBoxSetting, public VSSetting {
    Q_OBJECT
public:
    FreqTableSelector(const VideoSource& parent) : VSSetting(parent, "freqtable")
    {
        setLabel(QObject::tr("Channel frequency table"));
        addSelection("default");
        addSelection("us-cable");
        addSelection("us-bcast");
        addSelection("us-cable-hrc");
        addSelection("japan-bcast");
        addSelection("japan-cable");
        addSelection("europe-west");
        addSelection("europe-east");
        addSelection("italy");
        addSelection("newzealand");
        addSelection("australia");
        addSelection("ireland");
        addSelection("france");
        addSelection("china-bcast");
        addSelection("southafrica");
        addSelection("argentina");
        addSelection("australia-optus");
        setHelpText(QObject::tr("Use default unless this source uses a "
                    "different frequency table than the system wide table "
                    "defined in the General settings."));
    }

protected:
    QString freq;
};

class XMLTV_na_config: public VerticalConfigurationGroup {
    Q_OBJECT
public:
    XMLTV_na_config(const VideoSource& _parent): parent(_parent) {
        setLabel(QObject::tr("tv_grab_na configuration"));
        postalcode = new PostalCode();
        addChild(postalcode);

        provider = new ProviderSelector("tv_grab_na");
        addChild(provider);

        connect(postalcode, SIGNAL(valueChanged(const QString&)),
                this, SLOT(fillProviderSelections(const QString&)));
    };

    virtual void save(QSqlDatabase* db);

protected slots:
     void fillProviderSelections(const QString& maybePostalCode) {
         if (QRegExp("\\d{5}").exactMatch(maybePostalCode) ||
             QRegExp("[a-z]\\d[a-z]\\s?\\d[a-z]\\d", false).exactMatch(maybePostalCode))
         {
         	 QString mpc = maybePostalCode;
         	 mpc = mpc.replace(QRegExp(" "), "");
                 provider->fillSelections(mpc);
         }
     }

protected:
    const VideoSource& parent;
    PostalCode* postalcode;
    ProviderSelector* provider;
};

class XMLTV_uk_config: public VerticalConfigurationGroup {
public:
    XMLTV_uk_config(const VideoSource& _parent): parent(_parent) {
        setLabel(QObject::tr("tv_grab_uk configuration"));
        region = new RegionSelector();
        addChild(region);

        provider = new ProviderSelector("tv_grab_uk");
        addChild(provider);

        connect(region, SIGNAL(valueChanged(const QString&)),
                provider, SLOT(fillSelections(const QString&)));
    };

    virtual void save(QSqlDatabase* db);

protected:
    const VideoSource& parent;
    RegionSelector* region;
    ProviderSelector* provider;
};

class XMLTV_generic_config: public LabelSetting {
public:
    XMLTV_generic_config(const VideoSource& _parent, QString _grabber):
        parent(_parent),
        grabber(_grabber) {
        setLabel(grabber);
        setValue(QObject::tr("Configuration will run in the terminal window"));
    };

    virtual void load(QSqlDatabase* db) { (void)db; };
    virtual void save(QSqlDatabase* db);

protected:
    const VideoSource& parent;
    QString grabber;
};

class XMLTVConfig: public VerticalConfigurationGroup, public TriggeredConfigurationGroup {
public:
    XMLTVConfig(const VideoSource& parent) {
        XMLTVGrabber* grabber = new XMLTVGrabber(parent);
        addChild(grabber);
        setTrigger(grabber);

        // only save settings for the selected grabber
        setSaveAll(false);

        addTarget("tv_grab_na", new XMLTV_na_config(parent));
        grabber->addSelection("North America (xmltv)", "tv_grab_na");

        addTarget("tv_grab_de", new XMLTV_generic_config(parent, "tv_grab_de"));
        grabber->addSelection("Germany/Austria", "tv_grab_de");

        addTarget("tv_grab_sn", new XMLTV_generic_config(parent, "tv_grab_sn"));
        grabber->addSelection("Sweden/Norway","tv_grab_sn");

        addTarget("tv_grab_uk", new XMLTV_generic_config(parent, "tv_grab_uk"));
        grabber->addSelection("United Kingdom","tv_grab_uk");

        addTarget("tv_grab_uk_rt", new XMLTV_generic_config(parent, "tv_grab_uk_rt"));
        grabber->addSelection("United Kingdom (alternative)","tv_grab_uk_rt");

        addTarget("tv_grab_au", new XMLTV_generic_config(parent, "tv_grab_au"));
        grabber->addSelection("Australia", "tv_grab_au");

        addTarget("tv_grab_nz", new XMLTV_generic_config(parent, "tv_grab_nz"));
        grabber->addSelection("New Zealand", "tv_grab_nz");

        addTarget("tv_grab_fi", new XMLTV_generic_config(parent, "tv_grab_fi"));
        grabber->addSelection("Finland", "tv_grab_fi");

        addTarget("tv_grab_es", new XMLTV_generic_config(parent, "tv_grab_es"));
        grabber->addSelection("Spain", "tv_grab_es");

        addTarget("tv_grab_nl", new XMLTV_generic_config(parent, "tv_grab_nl"));
        grabber->addSelection("Holland", "tv_grab_nl");

        addTarget("tv_grab_dk", new XMLTV_generic_config(parent, "tv_grab_dk"));
        grabber->addSelection("Denmark", "tv_grab_dk");

        addTarget("tv_grab_fr", new XMLTV_generic_config(parent, "tv_grab_fr"));
        grabber->addSelection("France", "tv_grab_fr");

         addTarget("tv_grab_ja", new XMLTV_generic_config(parent, "tv_grab_ja"));
         grabber->addSelection("Japan", "tv_grab_ja");
    };
};

class VideoSource: public ConfigurationWizard {
public:
    VideoSource() {
        // must be first
        addChild(id = new ID());

        ConfigurationGroup *group = new VerticalConfigurationGroup(false);
        group->setLabel(QObject::tr("Video source setup"));
        group->addChild(name = new Name(*this));
        group->addChild(new XMLTVConfig(*this));
        group->addChild(new FreqTableSelector(*this));
        addChild(group);
    };
        
    int getSourceID(void) const { return id->intValue(); };

    void loadByID(QSqlDatabase* db, int id);

    static void fillSelections(QSqlDatabase* db, SelectSetting* setting);
    static QString idToName(QSqlDatabase* db, int id);

    QString getSourceName(void) const { return name->getValue(); };

    virtual void save(QSqlDatabase* db) {
        if (name)
            ConfigurationWizard::save(db);
    };

private:
    class ID: virtual public IntegerSetting,
              public AutoIncrementStorage {
    public:
        ID():
            AutoIncrementStorage("videosource", "sourceid") {
            setName("VideoSourceName");
            setVisible(false);
        };
        virtual QWidget* configWidget(ConfigurationGroup *cg, 
                                      QWidget* parent,
                                      const char* widgetName = 0) {
            (void)cg; (void)parent; (void)widgetName;
            return NULL;
        };
    };
    class Name: virtual public VSSetting,
                virtual public LineEditSetting {
    public:
        Name(const VideoSource& parent):
            VSSetting(parent, "name") {
            setLabel(QObject::tr("Video source name"));
        };
    };

private:
    ID* id;
    Name* name;
};

class CaptureCard;
class CCSetting: virtual public Setting, public SimpleDBStorage {
protected:
    CCSetting(const CaptureCard& _parent, QString name):
        SimpleDBStorage("capturecard", name),
        parent(_parent) {
        setName(name);
    };

    int getCardID(void) const;

protected:
    virtual QString setClause(void);
    virtual QString whereClause(void);
private:
    const CaptureCard& parent;
};

class DvbSatSetting: virtual public Setting, public SimpleDBStorage {
protected:
    DvbSatSetting(const CaptureCard& _parent, int _satnum, QString name):
        SimpleDBStorage("dvb_sat", name),
        parent(_parent), satnum(_satnum) {
        setName(name);
    };

    int getCardID(void) const;
    int getSatNum(void) const;

protected:
    virtual QString setClause(void);
    virtual QString whereClause(void);
private:
    const CaptureCard& parent;
    int satnum;
};

class VideoDevice: public PathSetting, public CCSetting {
public:
    VideoDevice(const CaptureCard& parent):
        PathSetting(true),
        CCSetting(parent, "videodevice") {
        setLabel(QObject::tr("Video device"));
        QDir dev("/dev", "video*", QDir::Name, QDir::System);
        fillSelectionsFromDir(dev);
        dev.setPath("/dev/v4l");
        fillSelectionsFromDir(dev);
        dev.setPath("/dev/dtv");
        fillSelectionsFromDir(dev);
    };

    static QStringList probeInputs(QString device);
};

class VbiDevice: public PathSetting, public CCSetting {
public:
    VbiDevice(const CaptureCard& parent):
        PathSetting(true),
        CCSetting(parent, "vbidevice") {
        setLabel(QObject::tr("VBI device"));
        QDir dev("/dev", "vbi*", QDir::Name, QDir::System);
        fillSelectionsFromDir(dev);
        dev.setPath("/dev/v4l");
        fillSelectionsFromDir(dev);
    };
};

class AudioDevice: public PathSetting, public CCSetting {
public:
    AudioDevice(const CaptureCard& parent):
        PathSetting(true),
        CCSetting(parent, "audiodevice") {
        setLabel(QObject::tr("Audio device"));
        QDir dev("/dev", "dsp*", QDir::Name, QDir::System);
        fillSelectionsFromDir(dev);
        dev.setPath("/dev/sound");
        fillSelectionsFromDir(dev);
        addSelection(QObject::tr("(None)"), "/dev/null");
    };
};

class AudioRateLimit: public ComboBoxSetting, public CCSetting {
public:
    AudioRateLimit(const CaptureCard& parent):
        CCSetting(parent, "audioratelimit") {
        setLabel(QObject::tr("Audio sampling rate limit"));
        addSelection(QObject::tr("(None)"), "0");
        addSelection("32000");
        addSelection("44100");
        addSelection("48000");
    };
};

class TunerCardInput: public ComboBoxSetting, public CCSetting {
    Q_OBJECT
public:
    TunerCardInput(const CaptureCard& parent):
        CCSetting(parent, "defaultinput") {
        setLabel(QObject::tr("Default input"));
    };

public slots:
    void fillSelections(const QString& device);
};

class SkipBtAudio: public CheckBoxSetting, public CCSetting {
public:
    SkipBtAudio(const CaptureCard& parent):
    CCSetting(parent, "skipbtaudio") {
        setLabel(QObject::tr("Do not adjust BT audio volume."));
        setHelpText(QObject::tr("Check this option for budget BT878 based "
                    "DVB-T cards such as the AverTV DVB-T that require the "
                    "audio volume left alone."));
   };
};

class DVBCardNum: public SpinBoxSetting, public CCSetting {
public:
    DVBCardNum(const CaptureCard& parent):
        SpinBoxSetting(0,3,1),
        CCSetting(parent, "videodevice") {
        setLabel(QObject::tr("DVB Card Number"));
        setHelpText(QObject::tr("When you change this setting, the text below "
                    "should change to the name and type of your card. If the "
                    "card cannot be opened, an error message will be "
                    "displayed."));
    };
};

class DVBCardType: public LabelSetting, public TransientStorage {
public:
    DVBCardType() {
        setLabel(QObject::tr("Card Type"));
    };
};

class DVBCardName: public LabelSetting, public TransientStorage {
public:
    DVBCardName() {
        setLabel(QObject::tr("Card Name"));
    };
};

class DVBSwFilter: public CheckBoxSetting, public CCSetting {
public:
    DVBSwFilter(const CaptureCard& parent):
        CCSetting(parent, "dvb_swfilter") {
        setLabel(QObject::tr("Do NOT use DVB driver for filtering."));
        setHelpText(QObject::tr("(BROKEN) This option is used to get around "
                    "filtering limitations on some DVB cards."));
    };
};

class DVBRecordTS: public CheckBoxSetting, public CCSetting {
public:
    DVBRecordTS(const CaptureCard& parent):
        CCSetting(parent, "dvb_recordts") {
        setLabel(QObject::tr("Record the TS, not PS."));
        setHelpText(QObject::tr("This will make the backend not perform "
                    "Transport Stream to Program Stream conversion."));
    };
};

class DVBNoSeqStart: public CheckBoxSetting, public CCSetting {
public:
    DVBNoSeqStart(const CaptureCard& parent):
        CCSetting(parent, "dvb_wait_for_seqstart") {
        setLabel(QObject::tr("Wait for SEQ start header."));
        setValue(true);
        setHelpText(QObject::tr("Normally the dvb-recording will drop packets "
                    "from the card untill a sequence start header is seen. "
                    "This option turns off this feature."));
    };
};

class DVBOnDemand: public CheckBoxSetting, public CCSetting {
public:
    DVBOnDemand(const CaptureCard& parent):
        CCSetting(parent, "dvb_on_demand") {
        setLabel(QObject::tr("Open DVB card on demand"));
        setValue(true);
        setHelpText(QObject::tr("This option makes the backend dvb-recorder "
                    "only open the card when it is actually in-use leaving "
                    "it free for other programs at other times."));
    };
};

class DVBPidBufferSize: public SpinBoxSetting, public CCSetting {
public:
    DVBPidBufferSize(const CaptureCard& parent):
        SpinBoxSetting(0, 180000, 188),
        CCSetting(parent, "dvb_dmx_buf_size") {
        setLabel(QObject::tr("Per PID driver buffer size"));
        setValue(188*50);
    };
};

class DVBBufferSize: public SpinBoxSetting, public CCSetting {
public:
    DVBBufferSize(const CaptureCard& parent):
        SpinBoxSetting(0, 188000, 188),
        CCSetting(parent, "dvb_pkt_buf_size") {
        setLabel(QObject::tr("Packet buffer"));
        setValue(188*100);
    };
};

class DVBAudioDevice: public LineEditSetting, public CCSetting {    
    Q_OBJECT
public:
    DVBAudioDevice(const CaptureCard& parent):
        CCSetting(parent,"audiodevice") {
        setVisible(false);
    };

    void save(QSqlDatabase* db) {
        changed = true;
        settingValue = "";
        SimpleDBStorage::save(db);
    };
};

class DVBVbiDevice: public LineEditSetting, public CCSetting {
    Q_OBJECT
public:
    DVBVbiDevice(const CaptureCard& parent):
        CCSetting(parent,"vbidevice") {
        setVisible(false);
    };
    void save(QSqlDatabase* db) {
        changed = true;
        settingValue = "";
        SimpleDBStorage::save(db);
    };
};

class DVBDefaultInput: public LineEditSetting, public CCSetting {
    Q_OBJECT
public:
    DVBDefaultInput(const CaptureCard& parent):
        CCSetting(parent,"defaultinput") {
        setVisible(false);
    };
    void save(QSqlDatabase* db) {
        changed = true;
        settingValue = "DVBInput";
        SimpleDBStorage::save(db);
    };
};

class DVBSignalChannelOptions: public ChannelOptionsDVB {
public:
    DVBSignalChannelOptions(ChannelID& id): ChannelOptionsDVB(id) {};
    void load(QSqlDatabase* db) {
        if (id.intValue() != 0)
            ChannelOptionsDVB::load(db);
    };
    void save(QSqlDatabase* db) { (void)db; };
};

class DVBChannels: public ComboBoxSetting {
    Q_OBJECT
public:
    DVBChannels() : db(NULL)
    {
        setLabel(QObject::tr("Channels"));
        setHelpText(QObject::tr("This box contains all channels from the "
                    "selected video source. Select a channel here and press "
                    "the 'Load and Tune' button to load the channel settings "
                    "into the previous screen and try to tune it."));
    }

    void save(QSqlDatabase* db) { (void)db; };
    void load(QSqlDatabase* _db) {
        db = _db;
        fillSelections("All");
    };
public slots:
    void fillSelections(const QString& videoSource);

private:
    QSqlDatabase* db;
};

class DVBInfoLabel: public LabelSetting, public TransientStorage {
    Q_OBJECT
public:
    DVBInfoLabel(QString label):
      LabelSetting(), TransientStorage() {
        setLabel(label);
    };
public slots:
    void set(int val) {
        setValue(QString("%1").arg(val));
    };
};

class DVBStatusLabel: public LabelSetting, public TransientStorage {
    Q_OBJECT
public:
    DVBStatusLabel() {
        setLabel(QObject::tr("Status"));
    };
public slots:
    void set(QString str) {
        setValue(str);
    };
};

class DVBSatelliteConfigType: public ComboBoxSetting, public CCSetting {
public:
    DVBSatelliteConfigType(CaptureCard& parent):
        CCSetting(parent, "dvb_sat_type") {
        setLabel(QObject::tr("Type"));
        addSelection("Single LNB","0");
        addSelection("Tone Switch aka Mini DiSEqC (2-Way)","1");
        addSelection("DiSEq v1.0 Switch (2-Way)","2");
        addSelection("DiSEq v1.1 Switch (2-Way)","3");
        addSelection("DiSEq v1.0 Switch (4-Way)","4");
        addSelection("DiSEq v1.1 Switch (4-Way)","5");
//        addSelection("DiSEqC Positioner","6");
        setHelpText(QObject::tr("Select the type of satellite equipment you "
                    "have. Selecting 'Finish' on this screen will only save "
                    "the type, and not the individual satellite, move down to "
                    "the list to do this."));
    };
};

class DVBSatelliteList: public ListBoxSetting, public TransientStorage {
    Q_OBJECT
public:
    DVBSatelliteList(CaptureCard& _parent): parent(_parent) {
        setLabel(QObject::tr("Satellites"));
        db = NULL;
        satellites = 1;
        setHelpText(QObject::tr("Select the satellite you want to configure "
                    "and press the 'menu' key, and edit the satellite, when "
                    "you are done configuring, press 'OK' to leave this "
                    "wizard."));
    };

    void load(QSqlDatabase* _db);

public slots:
    void fillSelections(const QString& v) {
        satellites = 1;
        if (v.toInt() > 0)
            satellites = 2;
        if (v.toInt() > 3)
            satellites = 4;
        if (db)
            load(db);
    };
protected:
    QSqlDatabase* db;
    CaptureCard& parent;
    int satellites;
};

class DVBSatelliteWizard: public ConfigurationWizard {
    Q_OBJECT
public:
    DVBSatelliteWizard(CaptureCard& _parent):
        parent(_parent) {
        VerticalConfigurationGroup* g = new VerticalConfigurationGroup(false);
        g->setLabel(QObject::tr("Satellite Configuration"));
        g->setUseLabel(false);
        DVBSatelliteConfigType* type = new DVBSatelliteConfigType(parent);
        list = new DVBSatelliteList(parent);
        connect(type, SIGNAL(valueChanged(const QString&)),
                list, SLOT(fillSelections(const QString&)));
        connect(list, SIGNAL(menuButtonPressed(int)),
                this, SLOT(editSat(int)));
        g->addChild(type);
        g->addChild(list);
        addChild(g);
    };

    class SatName: public LineEditSetting, public DvbSatSetting {
    public:
        SatName(const CaptureCard& parent, int satnum):
            DvbSatSetting(parent, satnum, "name") {
            setLabel(QObject::tr("Satellite Name"));
            setValue("Unnamed");
            setHelpText(QObject::tr("A textual representation of this "
                        "satellite or cluster of satellites."));
        };
    };

    class SatPos: public LineEditSetting, public DvbSatSetting {
    public:
        SatPos(const CaptureCard& parent, int satnum):
            DvbSatSetting(parent, satnum, "pos") {
            setLabel(QObject::tr("Satellite Position"));
            setValue("");
            setHelpText(QObject::tr("A textual representation of which "
                        "position the satellite is located at ('1W')"));
        };
    };

    class LofSwitch: public LineEditSetting, public DvbSatSetting {
    public:
        LofSwitch(const CaptureCard& parent, int satnum):
            DvbSatSetting(parent, satnum, "lnb_lof_switch") {
            setLabel(QObject::tr("LNB LOF Switch"));
            setValue("11700000");
            setHelpText(QObject::tr("This defines at what frequency (in hz) "
                        "the LNB will do a switch from high to low setting, "
                        "and vice versa."));
        };
    };

    class LofHigh: public LineEditSetting, public DvbSatSetting {
    public:
        LofHigh(const CaptureCard& parent, int satnum):
            DvbSatSetting(parent, satnum, "lnb_lof_hi") {
            setLabel(QObject::tr("LNB LOF High"));
            setValue("10600000");
            setHelpText(QObject::tr("This defines the offset (in hz) the "
                        "frequency coming from the lnb will be in high "
                        "setting."));
        };
    };

    class LofLow: public LineEditSetting, public DvbSatSetting {
    public:
        LofLow(const CaptureCard& parent, int satnum):
            DvbSatSetting(parent, satnum, "lnb_lof_lo") {
            setLabel(QObject::tr("LNB LOF Low"));
            setValue("9750000");
            setHelpText(QObject::tr("This defines the offset (in hz) the "
                        "frequency coming from the lnb will be in low "
                        "setting."));
        };
    };

    class SatEditor: public ConfigurationWizard {
    public:
        SatEditor(CaptureCard& _parent, int satnum):
            parent(_parent) {
            VerticalConfigurationGroup* g = new VerticalConfigurationGroup(false);
            g->addChild(new SatName(parent, satnum));
            g->addChild(new SatPos(parent, satnum));
            g->addChild(new LofSwitch(parent, satnum));
            g->addChild(new LofLow(parent, satnum));
            g->addChild(new LofHigh(parent, satnum));
            addChild(g);
        };
    protected:
        CaptureCard& parent;
    };

    int exec(QSqlDatabase* _db) {
        db = _db;
        return ConfigurationWizard::exec(db);
    };

public slots:
    void editSat(int satnum) {
        SatEditor ed(parent, satnum);
        ed.exec(db);
        list->load(db);
    };

protected:
    QSqlDatabase* db;
    CaptureCard& parent;
    DVBSatelliteList* list;
};

class DVBCardVerificationWizard: public ConfigurationWizard {
    Q_OBJECT
public:
    DVBCardVerificationWizard(int cardNum);
    ~DVBCardVerificationWizard(); 

    void load(QSqlDatabase* _db) {
        db = _db;
        ConfigurationWizard::load(db);
    };

private slots:
    void tuneConfigscreen();
    void tunePredefined();

private:
    QSqlDatabase* db;
    ChannelID cid;
#ifdef USING_DVB
    DVBChannel* chan;
    DVBSignalChannelOptions* dvbopts;
    DVBChannels* channels;
#endif
};

class DVBAdvConfigurationWizard: public ConfigurationWizard {
public:
    DVBAdvConfigurationWizard(CaptureCard& parent) {
        VerticalConfigurationGroup* rec = new VerticalConfigurationGroup(false);
        rec->setLabel(QObject::tr("Recorder Options"));
        rec->setUseLabel(false);

        rec->addChild(new DVBSwFilter(parent));
        rec->addChild(new DVBRecordTS(parent));
        rec->addChild(new DVBNoSeqStart(parent));
        rec->addChild(new DVBOnDemand(parent));
        rec->addChild(new DVBPidBufferSize(parent));
        rec->addChild(new DVBBufferSize(parent));
        addChild(rec);
    };
};

class DVBAdvancedConfigMenu: public ConfigurationPopupDialog,
                             public VerticalConfigurationGroup {
    Q_OBJECT
public:
    DVBAdvancedConfigMenu(CaptureCard& a_parent): parent(a_parent) {
        setLabel(QObject::tr("Configuration Options"));
        TransButtonSetting* advcfg = new TransButtonSetting();
        TransButtonSetting* verify = new TransButtonSetting();
        TransButtonSetting* satellite = new TransButtonSetting();

        advcfg->setLabel(QObject::tr("Advanced Configuration"));
        verify->setLabel(QObject::tr("Card Verification Wizard"));
        satellite->setLabel(QObject::tr("Satellite Configuration"));

        addChild(advcfg);
        addChild(verify);
        addChild(satellite);

        connect(advcfg, SIGNAL(pressed()), this, SLOT(execACW()));
        connect(verify, SIGNAL(pressed()), this, SLOT(execCVW()));
        connect(satellite, SIGNAL(pressed()), this, SLOT(execSAT()));
    };

    void exec(QSqlDatabase* _db) {
        db = _db;
        ConfigurationPopupDialog::exec(db);
    };

public slots:
    void execCVW();
    void execACW();
    void execSAT();

private:
    CaptureCard& parent;
    QSqlDatabase* db;
};

class CardType: public ComboBoxSetting, public CCSetting {
public:
    CardType(const CaptureCard& parent);
    static void fillSelections(SelectSetting* setting);
};

class V4LConfigurationGroup: public VerticalConfigurationGroup {
public:
    V4LConfigurationGroup(CaptureCard& a_parent):
        VerticalConfigurationGroup(false, true),
        parent(a_parent) {
        setUseLabel(false);

        VideoDevice* device;
        TunerCardInput* input;

        addChild(device = new VideoDevice(parent));
        addChild(new VbiDevice(parent));
        addChild(new AudioDevice(parent));
        addChild(new AudioRateLimit(parent));
        addChild(new SkipBtAudio(parent));
        addChild(input = new TunerCardInput(parent));

        connect(device, SIGNAL(valueChanged(const QString&)),
                input, SLOT(fillSelections(const QString&)));
        input->fillSelections(device->getValue());
    };
private:
    CaptureCard& parent;
};

class DVBConfigurationGroup: public VerticalConfigurationGroup {
    Q_OBJECT
public:
    DVBConfigurationGroup(CaptureCard& a_parent);

    void load(QSqlDatabase* _db) {
        db = _db;
        VerticalConfigurationGroup::load(db);
    };

public slots:
    void probeCard(const QString& cardNumber);

private:
    CaptureCard& parent;

    QSqlDatabase *db;
    DVBCardName* cardname;
    DVBCardType* cardtype;
    TransButtonSetting *advcfg;
};

class CaptureCardGroup: public VerticalConfigurationGroup,
                        public TriggeredConfigurationGroup {
    Q_OBJECT
public:
    CaptureCardGroup(CaptureCard& parent):
        VerticalConfigurationGroup(false, true) {
        CardType* cardtype = new CardType(parent);
        addChild(cardtype);
        setTrigger(cardtype);
        setSaveAll(false);

        addTarget("V4L", new V4LConfigurationGroup(parent));
        addTarget("DVB", new DVBConfigurationGroup(parent));
    };

protected slots:
    virtual void triggerChanged(const QString& value) {
        QString own = value;
        if (own == "HDTV" || own == "MPEG" || own == "MJPEG")
            own = "V4L";
        TriggeredConfigurationGroup::triggerChanged(own);
    };
};

class CaptureCard: public ConfigurationWizard {
    Q_OBJECT
public:
    CaptureCard() {
        // must be first
        addChild(id = new ID());

        CaptureCardGroup *cardgroup = new CaptureCardGroup(*this);
        addChild(cardgroup);

        addChild(new Hostname(*this));
    };

    int getCardID(void) const {
        return id->intValue();
    };

    QString getDvbCard() { return dvbCard; };

    void loadByID(QSqlDatabase* db, int id);

    static void fillSelections(QSqlDatabase* db, SelectSetting* setting);

    void load(QSqlDatabase* _db) {
        db = _db;
        ConfigurationWizard::load(db);
    };

public slots:
    void execDVBConfigMenu();
    void setDvbCard(const QString& card) { dvbCard = card; };

private:
    class ID: virtual public IntegerSetting,
              public AutoIncrementStorage {
    public:
        ID():
            AutoIncrementStorage("capturecard", "cardid") {
            setVisible(false);
            setName("ID");
        };
    };

    class Hostname: public HostnameSetting, public CCSetting {
    public:
        Hostname(const CaptureCard& parent): CCSetting(parent, "hostname") {};
    };

private:
    ID* id;
    QSqlDatabase* db;
    QString dvbCard;
};

class CardInput;
class CISetting: virtual public Setting, public SimpleDBStorage {
protected:
    CISetting(const CardInput& _parent, QString name):
        SimpleDBStorage("cardinput", name),
        parent(_parent) {
        setName(name);
    };

    int getInputID(void) const;

    void fillSelections(QSqlDatabase* db);

protected:
    virtual QString setClause(void);
    virtual QString whereClause(void);
private:
    const CardInput& parent;
};

class CardID: public SelectLabelSetting, public CISetting {
public:
    CardID(const CardInput& parent):
        CISetting(parent, "cardid") {
        setLabel(QObject::tr("Capture device"));
    };

    virtual void load(QSqlDatabase* db) {
        fillSelections(db);
        CISetting::load(db);
    };

    void fillSelections(QSqlDatabase* db) {
        CaptureCard::fillSelections(db, this);
    };
};


class SourceID: public ComboBoxSetting, public CISetting {
public:
    SourceID(const CardInput& parent):
        CISetting(parent, "sourceid") {
        setLabel(QObject::tr("Video source"));
        addSelection(QObject::tr("(None)"), "0");
    };

    virtual void load(QSqlDatabase* db) {
        fillSelections(db);
        CISetting::load(db);
    };

    void fillSelections(QSqlDatabase* db) {
        clearSelections();
        addSelection(QObject::tr("(None)"), "0");
        VideoSource::fillSelections(db, this);
    };
};

class InputName: public LabelSetting, public CISetting {
public:
    InputName(const CardInput& parent):
        CISetting(parent, "inputname") {
        setLabel(QObject::tr("Input"));
    };
};

class ExternalChannelCommand: public LineEditSetting, public CISetting {
public:
    ExternalChannelCommand(const CardInput& parent):
        CISetting(parent,"externalcommand") {
        setLabel(QObject::tr("External channel change command"));
        setValue("");
        setHelpText(QObject::tr("If specified, this command will be run to "
                    "change the channel for inputs which do not have a tuner.  "
                    "The first argument will be the channel number."));
    };
};

class PresetTuner: public LineEditSetting, public CISetting {
public:
    PresetTuner(const CardInput& parent):
        CISetting(parent, "tunechan") {
        setLabel(QObject::tr("Preset tuner to channel"));
        setValue("");
        setHelpText(QObject::tr("If specified, the tuner will change to this "
                    "channel when the input is selected.  This is only "
                    "useful if you use your tuner input with an external "
                    "channel changer."));
    };
};

class StartingChannel: public LineEditSetting, public CISetting {
public:
    StartingChannel(const CardInput& parent):
        CISetting(parent, "startchan") {
        setLabel(QObject::tr("Starting channel"));
        setValue("3");
        setHelpText(QObject::tr("LiveTV will change to the above channel when "
                    "the input is first selected."));
    };
};

class InputPreference: public SpinBoxSetting, public CISetting {
public:
    InputPreference(const CardInput& parent):
        SpinBoxSetting(-99,99,1),
        CISetting(parent, "preference") {
        setLabel(QObject::tr("Input preference"));
        setHelpText(QObject::tr("If the input preference is not equal for "
                    "all inputs, the scheduler may choose to record a show "
                    "at a later time so that it can record on an input with "
                    "a higher value."));
    };
};

class CardInput: public ConfigurationWizard {
public:
    CardInput() {
        addChild(id = new ID());

        ConfigurationGroup *group = new VerticalConfigurationGroup(false);
        group->setLabel(QObject::tr("Connect source to input"));
        group->addChild(cardid = new CardID(*this));
        group->addChild(inputname = new InputName(*this));
        group->addChild(sourceid = new SourceID(*this));
        group->addChild(new InputPreference(*this));
        group->addChild(new ExternalChannelCommand(*this));
        group->addChild(new PresetTuner(*this));
        group->addChild(new StartingChannel(*this));
        addChild(group);
    };

    int getInputID(void) const { return id->intValue(); };

    void loadByID(QSqlDatabase* db, int id);
    void loadByInput(QSqlDatabase* db, int cardid, QString input);
    QString getSourceName(void) const { return sourceid->getSelectionLabel(); };

    virtual void save(QSqlDatabase* db);

private:
    class ID: virtual public IntegerSetting,
              public AutoIncrementStorage {
    public:
        ID():
            AutoIncrementStorage("cardinput", "cardid") {
            setVisible(false);
            setName("CardInputID");
        };
        virtual QWidget* configWidget(ConfigurationGroup *cg, QWidget* parent, 
                                      const char* widgetName = 0) {
            (void)cg; (void)parent; (void)widgetName;
            return NULL;
        };
    };

private:
    ID* id;
    CardID* cardid;
    InputName* inputname;
    SourceID* sourceid;
    QSqlDatabase* db;
};

class CaptureCardEditor: public ListBoxSetting, public ConfigurationDialog {
    Q_OBJECT
public:
    CaptureCardEditor(QSqlDatabase* _db):
        db(_db) {
        setLabel(tr("Capture cards"));
    };

    virtual MythDialog* dialogWidget(MythMainWindow* parent,
                                     const char* widgetName=0) {
        dialog=ConfigurationDialog::dialogWidget(parent, widgetName);
        connect(dialog, SIGNAL(menuButtonPressed()), this, SLOT(menu()));
        return dialog;
    };

    virtual int exec(QSqlDatabase* db);
    virtual void load(QSqlDatabase* db);
    virtual void save(QSqlDatabase* db) { (void)db; };

public slots:
    void menu() {
        if (getValue().toInt() == 0) {
            CaptureCard cc;
            cc.exec(db);
        } else {
            int val = MythPopupBox::show2ButtonPopup(gContext->GetMainWindow(),
                                                     "",
                                                     tr("Capture Card Menu"), 
                                                     tr("Edit.."),
                                                     tr("Delete.."), 1);

            if (val == 0)
                edit();
            else if (val == 1)
                del(); 
        }
    };

    void edit() {
        CaptureCard cc;
        if (getValue().toInt() != 0)
            cc.loadByID(db,getValue().toInt());
        cc.exec(db);
    };

    void del() {
        int val = MythPopupBox::show2ButtonPopup(gContext->GetMainWindow(), "",
                                          tr("Are you sure you want to delete "
                                             "this capture card?"), 
                                             tr("Yes, delete capture card"),
                                             tr("No, don't"), 2);
        if (val == 0)
        {
            QSqlQuery query;

            query = db->exec(QString("DELETE FROM capturecard"
                                     " WHERE cardid='%1'").arg(getValue()));
            if (!query.isActive())
                MythContext::DBError("Deleting Capture Card", query);

            query = db->exec(QString("DELETE FROM cardinput"
                                     " WHERE cardid='%1'").arg(getValue()));
            if (!query.isActive())
                MythContext::DBError("Deleting Card Input", query);
            load(db);
        }
    };

protected:
    QSqlDatabase* db;
};

class VideoSourceEditor: public ListBoxSetting, public ConfigurationDialog {
    Q_OBJECT
public:
    VideoSourceEditor(QSqlDatabase* _db):
        db(_db) {
        setLabel(tr("Video sources"));
    };

    virtual MythDialog* dialogWidget(MythMainWindow* parent,
                                     const char* widgetName=0) {
        dialog = ConfigurationDialog::dialogWidget(parent, widgetName);
        connect(dialog, SIGNAL(menuButtonPressed()), this, SLOT(menu()));
        return dialog;
    };

    virtual int exec(QSqlDatabase* db);
    virtual void load(QSqlDatabase* db);
    virtual void save(QSqlDatabase* db) { (void)db; };

public slots:
    void menu() {
        if (getValue().toInt() == 0) {
            VideoSource vs;
            vs.exec(db);
        } else {
            int val = MythPopupBox::show2ButtonPopup(gContext->GetMainWindow(),
                                                     "",
                                                     tr("Video Source Menu"),
                                                     tr("Edit.."),
                                                     tr("Delete.."), 1);

            if (val == 0)
                emit edit();
            else if (val == 1)
                emit del();
        }
    };

    void edit() {
        VideoSource vs;
        if (getValue().toInt() != 0)
            vs.loadByID(db,getValue().toInt());

        vs.exec(db);
    };

    void del() {
        int val = MythPopupBox::show2ButtonPopup(gContext->GetMainWindow(), "",
                                           tr("Are you sure you want to delete "
                                              "this video source?"),
                                           tr("Yes, delete video source"),
                                           tr("No, don't"), 2);

        if (val == 0)
        {
            QSqlQuery query = db->exec(QString("DELETE FROM videosource"
                                               " WHERE sourceid='%1'")
                                               .arg(getValue()));
            if (!query.isActive())
                MythContext::DBError("Deleting VideoSource", query);
            load(db);
        }
    };

protected:
    QSqlDatabase* db;
};

class CardInputEditor: public ListBoxSetting, public ConfigurationDialog {
public:
    CardInputEditor(QSqlDatabase* _db):
        db(_db) {
        setLabel(QObject::tr("Input connections"));
    };
    virtual ~CardInputEditor();

    virtual int exec(QSqlDatabase* db);
    virtual void load(QSqlDatabase* db);
    virtual void save(QSqlDatabase* db) { (void)db; };

protected:
    vector<CardInput*> cardinputs;
    QSqlDatabase* db;
};

#endif
