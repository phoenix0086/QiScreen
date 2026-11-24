import { common } from '@kit.AbilityKit';
import { window } from '@kit.ArkUI';
import { preferences } from '@kit.ArkData';
import { privacyManager } from '@kit.AppGalleryKit';
import { util } from '@kit.ArkTS';
import receiver, { ReceiverStatus, ReceiverView } from 'libqiscreen.so';

interface DisplayPreset {
  label: string;
  width: number;
  height: number;
}

// Keep the order stable because the index is stored in preferences.
const DISPLAY_PRESETS: DisplayPreset[] = [
  { label: '1080p', width: 1920, height: 1080 },
  { label: '720p', width: 1280, height: 720 },
  { label: '2K', width: 2560, height: 1440 },
  { label: '4K', width: 3840, height: 2160 }
];
const DISPLAY_ORDER: number[] = [3, 2, 0, 1];
const FRAME_RATES: number[] = [30, 60];

const BACKGROUND: string = '#0B1423';
const PANEL: string = '#131F31';
const INSET: string = '#0E1929';
const STROKE: string = '#2D4056';
const INK: string = '#F1F5F2';
const MUTED: string = '#AABACB';
const ACCENT: string = '#89E4CF';
const WARNING: string = '#FFC0A2';

@Entry
@Component
struct Index {
  @State private deviceName: string = '栖屏';
  @State private listening: boolean = false;
  @State private streaming: boolean = false;
  @State private ready: boolean = false;
  @State private settingsLoaded: boolean = false;
  @State private busy: boolean = false;
  @State private showControls: boolean = true;
  @State private showDiagnostics: boolean = false;
  @State private showSettings: boolean = false;
  @State private layoutWidth: number = 380;
  @State private layoutHeight: number = 800;
  @State private nativeError: string = '';
  @State private actionError: string = '';
  @State private settingsError: string = '';
  @State private quality: number = 0;
  @State private fpsIndex: number = 1;
  @State private lowLatency: boolean = true;
  @State private streamWidth: number = 1920;
  @State private streamHeight: number = 1080;
  @State private client: string = '';
  @State private sourceAgeP95: number = -1;
  @State private audioFrames: number = 0;
  @State private showHelp: boolean = false;
  @State private showLicenses: boolean = false;
  @State private licenseText: string = '';
  @State private highQuality: boolean = false;
  @State private viewScale: number = 1;
  @State private viewPanX: number = 0;
  @State private viewPanY: number = 0;
  @State private videoCodec: string = 'H.264';
  @State private receiveFps: number = 0;
  @State private renderFps: number = 0;
  @State private bitrate: number = 0;
  @State private resolutionNote: string = '';
  private rateTime: number = 0;
  private rateReceived: number = 0;
  private rateShown: number = 0;
  private rateBytes: number = 0;
  private timer: number = -1;
  private pageActive: boolean = false;
  private windowUpdateSequence: number = 0;
  private windowUpdate: Promise<void> = Promise.resolve();
  private lastTouchVersion: number = 0;
  private controller: XComponentController = new XComponentController();
  private prefs?: preferences.Preferences;

  aboutToAppear(): void {
    this.pageActive = true;
    this.lastTouchVersion = receiver.viewStatus().touchVersion;
    receiver.setStateCallback((viewOnly: boolean) => {
      if (viewOnly) {
        this.refreshView();
      } else {
        this.refresh();
      }
    });
    this.loadSettings();
    this.refresh();
    // Events handle transitions immediately; this timer only refreshes statistics and acts as a fallback.
    this.timer = setInterval(() => this.refresh(), 1000);
  }
  aboutToDisappear(): void {
    console.info('QiScreen page disappearing');
    this.pageActive = false;
    receiver.setStateCallback(null);
    if (this.timer >= 0) clearInterval(this.timer);
    receiver.stop().catch(() => {});
    this.updateWindow(false);
  }
  onBackPress(): boolean {
    if (this.showLicenses) { this.showLicenses = false; return true; }
    if (this.showHelp) { this.showHelp = false; return true; }
    if (this.showSettings) { this.showSettings = false; return true; }
    if (this.streaming && this.viewScale > 1) { receiver.setZoom(1); return true; }
    if (this.streaming || this.listening) { this.toggleReceiver(); return true; }
    return false;
  }
  private async loadSettings(): Promise<void> {
    try {
      const context = this.getUIContext().getHostContext() as common.UIAbilityContext;
      this.prefs = await preferences.getPreferences(context, { name: 'qiscreen' });
      const name = await this.prefs.get('name', '栖屏');
      const quality = await this.prefs.get('quality', 0);
      const fps = await this.prefs.get('fps', 1);
      const lowLatency = await this.prefs.get('lowLatency', true);
      const highQuality = await this.prefs.get('highQuality', false);
      if (!this.pageActive) return;
      this.deviceName = typeof name === 'string' ? name.trim().slice(0, 20) || '栖屏' : '栖屏';
      this.quality = typeof quality === 'number' && Number.isInteger(quality) && DISPLAY_PRESETS[quality] ? quality : 0;
      this.fpsIndex = fps === 0 ? 0 : 1;
      this.highQuality = highQuality === true;
      receiver.setLowLatency(lowLatency !== false);
      this.refresh();
    } catch (e) { this.settingsError = '无法读取接收设置，已使用默认值。'; }
    finally { this.settingsLoaded = true; }
  }
  private refresh(): void {
    if (!this.pageActive) return;
    const state: ReceiverStatus = receiver.status();
    const wasStreaming = this.streaming;
    const wasListening = this.listening;
    this.ready = state.ready;
    this.listening = state.listening;
    this.streaming = state.streaming;
    this.streamWidth = state.width;
    this.streamHeight = state.height;
    this.client = state.client;
    this.lowLatency = state.lowLatency;
    this.sourceAgeP95 = state.sourceAgeP95Ms;
    this.audioFrames = state.audioFrames;
    this.applyView(state);
    this.videoCodec = state.videoCodec;
    this.resolutionNote = state.height < state.requestedHeight ? 'Mac 当前发送的分辨率低于所选目标' : '';
    const now: number = Date.now();
    if (!wasStreaming || !this.streaming || now < this.rateTime || state.receivedFrames < this.rateReceived
      || state.frames < this.rateShown || state.receivedBytes < this.rateBytes) {
      this.rateTime = now; this.rateReceived = state.receivedFrames;
      this.rateShown = state.frames; this.rateBytes = state.receivedBytes;
      this.receiveFps = 0; this.renderFps = 0; this.bitrate = 0;
    } else if (now - this.rateTime >= 1000) {
      const seconds: number = (now - this.rateTime) / 1000;
      this.receiveFps = (state.receivedFrames - this.rateReceived) / seconds;
      this.renderFps = (state.frames - this.rateShown) / seconds;
      this.bitrate = (state.receivedBytes - this.rateBytes) * 8 / seconds / 1000000;
      this.rateTime = now; this.rateReceived = state.receivedFrames;
      this.rateShown = state.frames; this.rateBytes = state.receivedBytes;
    }
    const newError = state.error.length > 0 && state.error !== this.nativeError;
    this.nativeError = state.error;
    if (this.streaming && !wasStreaming) {
      this.showSettings = false;
      this.showHelp = false;
      this.showControls = this.nativeError.length > 0;
      this.showDiagnostics = this.showControls;
      console.info(`QiScreen first-frame state notification: ${state.stateNotifyMs} ms`);
    } else if (!this.streaming && wasStreaming) {
      this.showControls = true;
      this.showDiagnostics = false;
    }
    if (newError && this.streaming) {
      this.showControls = true;
      this.showDiagnostics = true;
    }
    if (wasListening !== this.listening || wasStreaming !== this.streaming) {
      this.updateWindow(this.listening || this.streaming);
    }
  }
  private refreshView(): void {
    if (this.pageActive) this.applyView(receiver.viewStatus());
  }
  private applyView(state: ReceiverView): void {
    this.viewScale = state.viewScale;
    this.viewPanX = this.getUIContext().px2vp(state.viewPanX);
    this.viewPanY = this.getUIContext().px2vp(state.viewPanY);
    const taps: number = state.touchVersion - this.lastTouchVersion;
    this.lastTouchVersion = state.touchVersion;
    if (this.streaming && taps > 0 && taps % 2 === 1) {
      this.showControls = !this.showControls;
      if (!this.showControls) this.showDiagnostics = false;
    }
  }
  private updateWindow(keepAwake: boolean): void {
    const sequence = ++this.windowUpdateSequence;
    const stream = this.pageActive && this.streaming;
    // Serialize window writes so an older transition cannot finish after the newer one.
    this.windowUpdate = this.windowUpdate.then(async () => {
      if (sequence !== this.windowUpdateSequence) return;
      try {
        const context = this.getUIContext().getHostContext() as common.UIAbilityContext;
        const mainWindow = await window.getLastWindow(context);
        if (sequence !== this.windowUpdateSequence) return;
        await mainWindow.setWindowKeepScreenOn(keepAwake);
        if (sequence !== this.windowUpdateSequence) return;
        await mainWindow.setPreferredOrientation(stream ? window.Orientation.AUTO_ROTATION_LANDSCAPE
          : window.Orientation.AUTO_ROTATION);
        if (sequence !== this.windowUpdateSequence) return;
        await mainWindow.setWindowSystemBarEnable(stream ? [] : ['status', 'navigation']);
      } catch (e) { console.warn('QiScreen window update could not finish'); }
    });
  }

  private async changeLatencyMode(enabled: boolean): Promise<void> {
    if (this.lowLatency === enabled) return;
    receiver.setLowLatency(enabled);
    this.refresh();
    try {
      if (this.prefs) { await this.prefs.put('lowLatency', enabled); await this.prefs.flush(); }
    } catch (e) { console.warn('QiScreen display preference could not be saved'); }
  }

  private async toggleReceiver(): Promise<void> {
    if (this.busy || !this.settingsLoaded || (!this.listening && !this.streaming && !this.ready)) return;
    this.busy = true;
    this.actionError = '';
    this.showSettings = false;
    try {
      if (this.listening || this.streaming) {
        await receiver.stop();
        return;
      }
      this.deviceName = this.deviceName.trim().slice(0, 20) || '栖屏';
      const preset = DISPLAY_PRESETS[this.quality];
      const context = this.getUIContext().getHostContext() as common.UIAbilityContext;
      if (!await this.ensurePrivacyConsent(context) || !this.pageActive) return;
      const started = await receiver.start(this.deviceName, context.filesDir, preset.width, preset.height,
        FRAME_RATES[this.fpsIndex], this.highQuality);
      if (this.pageActive && started && this.prefs) {
        try {
          await this.prefs.put('name', this.deviceName);
          await this.prefs.put('quality', this.quality);
          await this.prefs.put('fps', this.fpsIndex);
          await this.prefs.put('highQuality', this.highQuality);
          await this.prefs.flush();
          this.settingsError = '';
        } catch (e) { this.settingsError = '无法保存接收设置，本次仍可使用。'; }
      }
    } catch (e) { this.actionError = '操作未完成，请重试。'; }
    finally {
      this.refresh();
      this.busy = false;
    }
  }

  private async ensurePrivacyConsent(context: common.UIAbilityContext): Promise<boolean> {
    try {
      let result = privacyManager.getAppPrivacyResult().find(item =>
        item.type === privacyManager.AppPrivacyType.PRIVACY_AGREEMENT);
      if (result?.result !== privacyManager.AppPrivacyResultType.FULL_MODE_AGREED) {
        await privacyManager.requestAppPrivacyConsent(context);
        result = privacyManager.getAppPrivacyResult().find(item =>
          item.type === privacyManager.AppPrivacyType.PRIVACY_AGREEMENT);
      }
      if (result?.result === privacyManager.AppPrivacyResultType.FULL_MODE_AGREED) return true;
      this.actionError = '同意隐私政策后才可开始接收。';
    } catch (e) {
      this.actionError = '暂时无法确认隐私签署状态，请检查华为应用市场服务后重试。';
    }
    return false;
  }

  private async openPrivacyPolicy(): Promise<void> {
    let url = 'https://agreement-drcn.hispace.dbankcloud.cn/index.html?lang=zh&agreementId=2088417307855475968';
    try {
      const policy = privacyManager.getAppPrivacyMgmtInfo().privacyInfo.find(item =>
        item.type === privacyManager.AppPrivacyLinkType.PRIVACY_STATEMENT_LINK);
      if (policy?.url) url = policy.url;
    } catch (e) { console.warn('QiScreen: using published privacy policy link'); }
    try {
      const context = this.getUIContext().getHostContext() as common.UIAbilityContext;
      await context.startAbility({ action: 'ohos.want.action.viewData', uri: url });
    } catch (e) { this.actionError = '隐私政策暂时无法打开，请稍后重试。'; }
  }

  private async openLicenses(): Promise<void> {
    this.showLicenses = true;
    try {
      const context = this.getUIContext().getHostContext() as common.UIAbilityContext;
      const data = await context.resourceManager.getRawFileContent('open-source-notices.txt');
      this.licenseText = util.TextDecoder.create('utf-8').decodeToString(data);
    } catch (e) { this.licenseText = '暂时无法读取开源声明，请重新打开。'; }
  }

  private statusText(): string {
    if (!this.settingsLoaded || (!this.ready && !this.listening)) return '正在准备接收';
    if (this.busy) return this.listening ? '正在处理接收服务' : '正在启动接收';
    if (this.listening) return '等待 Mac 连接';
    if (this.nativeError || this.actionError) return '接收服务遇到问题';
    return '尚未开始接收';
  }

  private latencyText(): string {
    if (this.sourceAgeP95 < 0) return '—';
    return Math.round(this.sourceAgeP95) + ' ms';
  }
  private latencyColor(): string {
    if (this.sourceAgeP95 < 0) return '#9AA3B8';
    if (this.sourceAgeP95 < 120) return '#1E9E6A';
    if (this.sourceAgeP95 < 250) return '#C99A2E';
    return '#D95F4C';
  }

  @Builder
  private SettingOption(label: string, isSelected: () => boolean, id: string, select: () => void) {
    Button(label).id(id).height(40).layoutWeight(1).padding(0).borderRadius(8)
      .fontSize(12).fontWeight(isSelected() ? FontWeight.Medium : FontWeight.Normal)
      .fontColor(isSelected() ? BACKGROUND : MUTED).backgroundColor(isSelected() ? ACCENT : Color.Transparent)
      .accessibilitySelected(isSelected()).onClick(select);
  }

  @Builder
  private DeviceSilhouette(label: string, desktop: boolean) {
    Column({ space: 10 }) {
      Stack({ alignContent: Alignment.Bottom }) {
        Column({ space: 0 }) {
          Stack({ alignContent: Alignment.Center }) {
            Rect().width(desktop ? 80 : 68).height(desktop ? 53 : 50).radius(desktop ? 6 : 9)
              .fill(PANEL).stroke(this.listening && !desktop ? ACCENT : '#6C8299').strokeWidth(2);
            if (!desktop) {
              Rect().width(28).height(2).radius(1).fill(this.listening ? ACCENT : '#6C8299')
                .translate({ y: 18 });
            }
          }.margin({ bottom: desktop ? 10 : 4 });
        }
        if (desktop) {
          Column() {
            Rect().width(2).height(8).fill('#6C8299');
            Rect().width(26).height(2).radius(1).fill('#6C8299');
          }
        }
      }.height(64).accessibilityLevel('no-hide-descendants');
      Text(label).fontSize(12).fontColor(MUTED);
    }.width(90);
  }

  @Builder
  private ConnectionCard() {
    Column({ space: 16 }) {
      Row({ space: 8 }) {
        Circle().width(8).height(8).fill(this.listening ? ACCENT
          : ((this.nativeError || this.actionError) ? WARNING : '#8195A9'));
        Text(this.statusText()).fontSize(13).fontWeight(FontWeight.Medium)
          .fontColor(this.listening ? ACCENT : MUTED);
      }.width('100%');

      Text(this.deviceName).width('100%').fontSize(this.layoutWidth >= 720 ? 40 : 34)
        .fontWeight(FontWeight.Bold).fontColor(INK).maxLines(2).textOverflow({ overflow: TextOverflow.Ellipsis });
      Text(this.listening ? '在 Mac 的「屏幕镜像」中选择上方设备。' : '把这台设备用作 Mac 的无线扩展屏。')
        .width('100%').fontSize(14).lineHeight(21).fontColor(MUTED);

      Row({ space: 4 }) {
        this.DeviceSilhouette('Mac', true);
        Row({ space: 4 }) {
          Divider().color(this.listening ? ACCENT : STROKE).layoutWeight(1);
          Circle().width(6).height(6).fill(this.listening ? ACCENT : '#8195A9');
          Divider().color(this.listening ? ACCENT : STROKE).layoutWeight(1);
        }.layoutWeight(1).translate({ y: -14 });
        this.DeviceSilhouette('本机', false);
      }.width('100%').justifyContent(FlexAlign.Center).padding({ left: 8, right: 8, top: 18, bottom: 18 })
        .backgroundColor(INSET).borderRadius(16).border({ width: 1, color: STROKE });

      if (this.actionError || this.nativeError || this.settingsError) {
        Text(this.actionError || this.nativeError || this.settingsError)
          .width('100%').fontSize(13).lineHeight(20).fontColor(WARNING);
      }

      Button(this.busy ? '正在处理…' : (this.listening ? '停止接收'
        : ((this.nativeError || this.actionError) ? '重试接收' : '开始接收')))
        .id('receiverToggle').width('100%').height(50).fontSize(16).fontWeight(FontWeight.Bold)
        .backgroundColor(this.listening ? INSET : ACCENT).fontColor(this.listening ? INK : BACKGROUND)
        .border({ width: this.listening ? 1 : 0, color: '#6C8299' }).borderRadius(14)
        .enabled(!this.busy && this.settingsLoaded && (this.ready || this.listening)).onClick(() => this.toggleReceiver());
    }.width('100%').alignItems(HorizontalAlign.Start).padding(this.layoutWidth >= 720 ? 28 : 20)
      .backgroundColor(PANEL).borderRadius(22).border({ width: 1, color: STROKE });
  }

  @Builder
  private ProfileValue(title: string, detail: () => string) {
    Column({ space: 5 }) {
      Text(title).fontSize(12).fontColor(MUTED);
      Text(detail()).fontSize(15).fontWeight(FontWeight.Medium).fontColor(INK);
    }.layoutWeight(1).alignItems(HorizontalAlign.Start);
  }

  @Builder
  private PreviewSettings() {
    Column({ space: 12 }) {
      Row() {
        Text('接收配置').fontSize(17).fontWeight(FontWeight.Bold).fontColor(INK);
        Blank();
        Button('设置').id('receiverSettings').height(44).fontSize(13)
          .backgroundColor(INSET).fontColor(INK).border({ width: 1, color: '#6C8299' })
          .enabled(!this.busy && this.settingsLoaded)
          .onClick(() => this.showSettings = !this.showSettings)
          .bindPopup(this.showSettings, {
            builder: this.ReceiverSettings,
            placement: Placement.BottomRight,
            popupColor: PANEL,
            radius: 20,
            enableArrow: true,
            onStateChange: (event) => { if (!event.isVisible) this.showSettings = false; }
          });
      }.width('100%');
      Row({ space: 8 }) {
        this.ProfileValue('目标分辨率', () => DISPLAY_PRESETS[this.quality].label);
        this.ProfileValue('目标帧率', () => FRAME_RATES[this.fpsIndex] + ' 帧');
        this.ProfileValue('视频编码', () => this.highQuality ? 'H.265 可选' : 'H.264');
      }.width('100%');
      Text(this.lowLatency ? '低延迟优先' : '完整播放').fontSize(12).fontColor(MUTED);
    }.width('100%').alignItems(HorizontalAlign.Start).padding(18)
      .backgroundColor(PANEL).borderRadius(20).border({ width: 1, color: STROKE });
  }

  @Builder
  private ConnectionGuide() {
    Column({ space: 12 }) {
      Text('连接步骤').fontSize(17).fontWeight(FontWeight.Bold).fontColor(INK);
      this.HelpStep('1', '连接同一局域网', '');
      this.HelpStep('2', '在 Mac 打开「屏幕镜像」', '控制中心 → 屏幕镜像 → 选择本机');
      this.HelpStep('3', '选择「扩展显示器」', '在 Mac 的「显示器」设置中调整屏幕排列');
    }.width('100%').alignItems(HorizontalAlign.Start).padding(18)
      .backgroundColor(PANEL).borderRadius(20).border({ width: 1, color: STROKE });
  }

  @Builder
  private Home() {
    Scroll() {
      Column({ space: 18 }) {
        Row({ space: 12 }) {
          Image($r('app.media.app_icon')).width(38).height(38).accessibilityLevel('no');
          Text('栖屏').fontSize(23).fontWeight(FontWeight.Bold).fontColor(INK);
          Blank();
          Button('使用说明').id('receiverHelp').height(44).fontSize(13)
            .backgroundColor(INSET).fontColor(INK).border({ width: 1, color: '#6C8299' })
            .onClick(() => this.showHelp = true);
        }.width('100%');
        if (this.layoutWidth >= 720) {
          Row({ space: 18 }) {
            Column() { this.ConnectionCard(); }.layoutWeight(6);
            Column({ space: 14 }) {
              this.PreviewSettings();
              this.ConnectionGuide();
            }.layoutWeight(5);
          }.width('100%').alignItems(VerticalAlign.Top);
        } else {
          this.ConnectionCard();
          this.PreviewSettings();
          this.ConnectionGuide();
        }
        Row({ space: 12 }) {
          Button('隐私政策').id('privacyPolicy').height(44).fontSize(12)
            .backgroundColor(INSET).fontColor(MUTED).onClick(() => this.openPrivacyPolicy());
          Button('开源声明').id('openSourceNotices').height(44).fontSize(12)
            .backgroundColor(INSET).fontColor(MUTED).onClick(() => this.openLicenses());
        }.width('100%').justifyContent(FlexAlign.Center);
      }.width('100%').constraintSize({ maxWidth: 1080 })
        .padding({ left: this.layoutWidth >= 720 ? 32 : 18, right: this.layoutWidth >= 720 ? 32 : 18,
          top: this.layoutWidth >= 720 ? 34 : 24, bottom: 24 });
    }.width('100%').height('100%').backgroundColor(BACKGROUND).scrollBar(BarState.Auto);
  }

  @Builder
  private HelpStep(num: string, title: string, detail: string) {
    Row({ space: 12 }) {
      Text(num).fontSize(13).fontColor(ACCENT).fontWeight(FontWeight.Bold)
        .width(26).height(26).textAlign(TextAlign.Center).borderRadius(13).backgroundColor(INSET);
      Column({ space: 4 }) {
        Text(title).fontColor(INK).fontSize(15).fontWeight(FontWeight.Medium);
        if (detail.length > 0) { Text(detail).fontColor(MUTED).fontSize(12).lineHeight(18); }
      }.alignItems(HorizontalAlign.Start).layoutWeight(1);
    }.width('100%').alignItems(VerticalAlign.Top);
  }

  @Builder
  private ReceiverSettings() {
    Column({ space: 14 }) {
      Row() {
        Text('接收设置').fontSize(17).fontWeight(FontWeight.Bold).fontColor(INK);
        Blank();
        Button('收起').id('closeReceiverSettings').height(32).fontSize(12)
          .backgroundColor(INSET).fontColor(ACCENT).onClick(() => this.showSettings = false);
      }.width('100%');
      Scroll() {
        Column({ space: 14 }) {
          Row() {
            Text('高效视频编码').fontSize(13).fontColor(MUTED);
            Blank();
            Toggle({ type: ToggleType.Switch, isOn: this.highQuality }).id('highQualityToggle')
              .selectedColor(ACCENT).onChange((value: boolean) => this.highQuality = value);
          }.width('100%');
          Text('4K 建议选 30 帧。高效编码支持 H.265；实际编码由 Mac 决定。')
            .fontSize(11).fontColor(MUTED);
          Text('设备名称').fontSize(13).fontColor(MUTED).fontWeight(FontWeight.Medium).width('100%');
          TextInput({ text: this.deviceName, placeholder: '设备名称' }).maxLength(20).height(44)
            .backgroundColor(INSET).fontColor(INK).placeholderColor('#9AA3B8').borderRadius(10)
            .onChange((value: string) => this.deviceName = value);
          Row({ space: 10 }) {
            Text('分辨率').fontSize(13).fontColor(MUTED).width(48);
            Row({ space: 4 }) {
              ForEach(DISPLAY_ORDER, (index: number) => {
                this.SettingOption(DISPLAY_PRESETS[index].label, () => this.quality === index,
                  index === 3 ? 'quality4k' : 'quality' + index, () => this.quality = index);
              }, (index: number) => index.toString());
            }.layoutWeight(1).backgroundColor(INSET).borderRadius(11).padding(3);
          }.width('100%');
          Row({ space: 10 }) {
            Text('帧率').fontSize(13).fontColor(MUTED).width(48);
            Row({ space: 4 }) {
              this.SettingOption('60 帧', () => this.fpsIndex === 1, 'fps60', () => this.fpsIndex = 1);
              this.SettingOption('30 帧', () => this.fpsIndex === 0, 'fps30', () => this.fpsIndex = 0);
            }.layoutWeight(1).backgroundColor(INSET).borderRadius(11).padding(3);
          }.width('100%');
          Row({ space: 10 }) {
            Text('策略').fontSize(13).fontColor(MUTED).width(48);
            Row({ space: 4 }) {
              this.SettingOption('低延迟优先', () => this.lowLatency, 'latencyLow', () => this.changeLatencyMode(true));
              this.SettingOption('完整播放', () => !this.lowLatency, 'latencyComplete', () => this.changeLatencyMode(false));
            }.layoutWeight(1).backgroundColor(INSET).borderRadius(11).padding(3);
          }.width('100%');
        }.width('100%').alignItems(HorizontalAlign.Start).enabled(!this.listening && !this.busy);
      }.width('100%').constraintSize({ maxHeight: Math.max(120, Math.min(340, this.layoutHeight - 180)) }).scrollBar(BarState.Auto);
      Text(this.listening ? '接收中，请先停止接收后修改画面设置。' : '画面设置在下一次连接时生效。')
        .fontSize(11).fontColor(MUTED).width('100%');
    }.id('receiverSettingsPanel').padding(20).width(Math.min(340, this.layoutWidth - 32)).backgroundColor(PANEL).borderRadius(20);
  }

  build() {
    Stack({ alignContent: Alignment.Center }) {
      XComponent({ id: 'qiscreenSurface', type: XComponentType.SURFACE, libraryname: 'qiscreen', controller: this.controller })
        .width('100%').height('100%').backgroundColor(Color.Black)
        .scale({ x: this.viewScale, y: this.viewScale })
        .translate({ x: this.viewPanX, y: this.viewPanY });

      if (!this.streaming) {
        this.Home();
      }

      if (this.streaming && this.showControls) {
        Column({ space: 8 }) {
          if (this.showDiagnostics) {
            Column({ space: 14 }) {
              Row() {
                Column({ space: 4 }) {
                  Text(this.client || 'Mac 已连接').fontSize(14).fontWeight(FontWeight.Medium).fontColor(INK);
                  Text(this.streamWidth + ' × ' + this.streamHeight + '  /  ' + this.videoCodec)
                    .fontSize(11).fontColor(MUTED);
                }.alignItems(HorizontalAlign.Start);
                Blank();
                Button(this.lowLatency ? '低延迟' : '完整播放').id('latencyModeToggle').height(36).fontSize(12)
                  .backgroundColor('#303747').fontColor(ACCENT).onClick(() => this.changeLatencyMode(!this.lowLatency));
              }.width('100%');
              Row() {
                Column({ space: 4 }) {
                  Text(this.receiveFps.toFixed(0) + ' fps').fontSize(22).fontWeight(FontWeight.Medium).fontColor(INK);
                  Text('实际接收').fontSize(10).fontColor(MUTED);
                }.layoutWeight(1).alignItems(HorizontalAlign.Start);
                Column({ space: 4 }) {
                  Text(this.renderFps.toFixed(0) + ' fps').fontSize(22).fontWeight(FontWeight.Medium).fontColor(INK);
                  Text('提交显示').fontSize(10).fontColor(MUTED);
                }.layoutWeight(1).alignItems(HorizontalAlign.Start);
                Column({ space: 4 }) {
                  Text(this.bitrate.toFixed(1)).fontSize(22).fontWeight(FontWeight.Medium).fontColor(INK);
                  Text('Mbps').fontSize(10).fontColor(MUTED);
                }.layoutWeight(1).alignItems(HorizontalAlign.Start);
              }.id('streamRates').width('100%');
              Row() {
                Text('接收时间差 ' + this.latencyText()).fontSize(11).fontColor(MUTED);
                Blank();
                Circle().width(5).height(5).fill(this.audioFrames > 0 ? '#6BD4B0' : '#788194');
                Text(this.audioFrames > 0 ? '音频已接收' : '无音频').fontSize(11).fontColor(MUTED).margin({ left: 5 });
              }.width('100%');
              Row() {
                Text('显示范围').fontSize(11).fontColor(MUTED);
                Blank();
                Button('裁切铺满').id('cropFillView').height(34).fontSize(12)
                  .backgroundColor('#303747').fontColor(ACCENT)
                  .onClick(() => { receiver.fillView(); this.showDiagnostics = false; });
              }.width('100%');
              Text('裁切铺满会放大并隐藏边缘；点「适应」恢复完整桌面。')
                .fontSize(11).fontColor(MUTED).width('100%');
              if (this.resolutionNote.length > 0 || (this.nativeError || this.actionError).length > 0) {
                Text(this.actionError || this.nativeError || this.resolutionNote).fontSize(12).fontColor('#F4C9A2').width('100%');
              }
            }.padding(18).width('100%').backgroundColor('#F2131F31').borderRadius(20);
          }
          Row({ space: 4 }) {
            Button('−').id('zoomOut').width(40).height(42).padding(0).fontSize(22)
              .backgroundColor(Color.Transparent).fontColor(INK).enabled(this.viewScale > 1)
              .onClick(() => receiver.setZoom(this.viewScale / 1.25));
            Button(Math.round(this.viewScale * 100) + '%').id('zoomReset').width(74).height(42).padding(0).fontSize(13)
              .backgroundColor(Color.Transparent).fontColor(ACCENT).onClick(() => receiver.setZoom(1));
            Button('＋').id('zoomIn').width(40).height(42).padding(0).fontSize(20)
              .backgroundColor(Color.Transparent).fontColor(INK).enabled(this.viewScale < 4)
              .onClick(() => receiver.setZoom(this.viewScale * 1.25));
            Divider().vertical(true).height(18).color('#434A59').margin({ left: 4, right: 4 });
            Button('适应').id('fitView').layoutWeight(1).height(42).padding(0).fontSize(12)
              .backgroundColor(this.viewScale === 1 ? '#343B4C' : Color.Transparent).fontColor(INK)
              .onClick(() => receiver.setZoom(1));
            Button('诊断').id('diagnosticsToggle').layoutWeight(1).height(42).padding(0).fontSize(12)
              .backgroundColor(this.showDiagnostics ? '#343B4C' : Color.Transparent).fontColor(INK)
              .onClick(() => this.showDiagnostics = !this.showDiagnostics);
            Button('断开').id('disconnectReceiver').layoutWeight(1).height(42).padding(0).fontSize(12)
              .backgroundColor(Color.Transparent).fontColor('#F1ADB2').enabled(!this.busy).onClick(() => this.toggleReceiver());
          }.padding({ left: 8, right: 8, top: 5, bottom: 5 }).width('100%')
            .backgroundColor('#F2131F31').borderRadius(26);
        }.width('94%').constraintSize({ maxWidth: 480 })
          .position({ x: '50%', y: '100%' }).markAnchor({ x: '50%', y: '100%' }).translate({ y: -24 });
      }

      if (this.showLicenses) {
        Column({ space: 12 }) {
          Row() {
            Text('开源声明').fontSize(18).fontWeight(FontWeight.Bold).fontColor(INK);
            Blank();
            Button('关闭').height(44).fontColor(ACCENT).backgroundColor(INSET)
              .onClick(() => this.showLicenses = false);
          }.width('100%');
          Scroll() {
            Text(this.licenseText).fontSize(12).fontColor(INK).width('100%');
          }.layoutWeight(1).scrollBar(BarState.Auto);
        }.padding(20).width('100%').height('100%').backgroundColor(BACKGROUND);
      }

      if (this.showHelp) {
        Stack({ alignContent: Alignment.Center }) {
          Column().width('100%').height('100%').backgroundColor('#B009101D')
            .onClick(() => this.showHelp = false);
          Scroll() {
          Column({ space: 16 }) {
            Row() {
              Text('使用说明').fontSize(18).fontWeight(FontWeight.Bold).fontColor(INK);
              Blank();
              Text('×').fontSize(20).fontColor(MUTED)
                .width(32).height(32).textAlign(TextAlign.Center).borderRadius(16)
                .backgroundColor(INSET).onClick(() => this.showHelp = false);
            }.width('100%');
            this.HelpStep('1', '连接同一局域网', 'Mac 可用网线或 Wi-Fi，平板连接同一局域网。保持栖屏在前台，接收时屏幕会常亮。');
            this.HelpStep('2', '打开 Mac 屏幕镜像', 'Mac 控制中心 → 屏幕镜像 → 选择「' + this.deviceName + '」。');
            this.HelpStep('3', '设为扩展显示器', '在 Mac「系统设置 → 显示器」里将其设为扩展屏，并在「排列」中拖到主屏旁边挨着；也可选镜像。连接后双击画面显示或隐藏控制栏，单击不触发。双指可放大局部，放大后单指拖动，点百分比或点「适应」恢复完整桌面。');
            Text('默认等比例完整显示，屏幕比例不同时会留黑边；诊断里的裁切铺满会隐藏边缘。分辨率为请求目标，实际接收值显示在控制栏。静止桌面的实收帧率会降低，属正常情况。局部放大不增加原画面细节；投屏会经过实时压缩，画质可能低于直接播放。')
              .fontSize(11).fontColor(MUTED).lineHeight(17);
          }.padding(22).width('100%');
          }.backgroundColor(PANEL).borderRadius(20).border({ width: 1, color: STROKE })
          .width(Math.min(460, this.layoutWidth - 32))
          .constraintSize({ maxHeight: Math.max(120, this.layoutHeight - 32) }).align(Alignment.Center);
        }.width('100%').height('100%');
      }
    }.width('100%').height('100%').backgroundColor(BACKGROUND).clip(true)
    .onAreaChange((_old: Area, area: Area) => {
      this.layoutWidth = Number(area.width);
      this.layoutHeight = Number(area.height);
      const ui: UIContext = this.getUIContext();
      receiver.setViewport(ui.vp2px(Number(area.width)), ui.vp2px(Number(area.height)),
        ui.vp2px(Number(area.globalPosition.x)), ui.vp2px(Number(area.globalPosition.y)));
    });
  }
}
