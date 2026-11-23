export interface ReceiverView {
  viewScale: number;
  viewPanX: number;
  viewPanY: number;
  touchVersion: number;
}
export interface ReceiverStatus {
  ready: boolean;
  listening: boolean;
  streaming: boolean;
  frames: number;
  dropped: number;
  audioFrames: number;
  width: number;
  height: number;
  port: number;
  error: string;
  client: string;
  serviceReadyMs: number;
  decoderSetupMs: number;
  firstFrameMs: number;
  stateNotifyMs: number;
  lowLatency: boolean;
  queueP95Ms: number;
  decodeP95Ms: number;
  sourceAgeP95Ms: number;
  queuedFrames: number;
  ntpRttMs: number;
  decodedFrames: number;
  skippedFrames: number;
  touchVersion: number;
  videoCodec: string;
  hardwareVideo: boolean;
  requestedWidth: number;
  requestedHeight: number;
  receivedFrames: number;
  receivedBytes: number;
  viewScale: number;
  viewPanX: number;
  viewPanY: number;
}
export const start: (name: string, filesDir: string, width: number, height: number, fps: number, highQuality: boolean) => Promise<boolean>;
export const stop: () => Promise<boolean>;
export const status: () => ReceiverStatus;
export const setStateCallback: (callback: ((viewOnly: boolean) => void) | null) => void;
export const setLowLatency: (enabled: boolean) => void;

export const setViewport: (width: number, height: number, originX: number, originY: number) => void;
export const setZoom: (scale: number) => void;

export const fillView: () => void;


export const viewStatus: () => ReceiverView;
