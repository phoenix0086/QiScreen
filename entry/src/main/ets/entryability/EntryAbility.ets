import { UIAbility } from '@kit.AbilityKit';
import { window } from '@kit.ArkUI';
import receiver from 'libqiscreen.so';

export default class EntryAbility extends UIAbility {
  onWindowStageCreate(stage: window.WindowStage): void {
    try {
      stage.loadContent('pages/Index');
    } catch (e) {
      console.error('QiScreen: unable to load receiver page');
    }
  }
  onBackground(): void {
    console.info('QiScreen ability background');
    // The visible screen is the receiver. Do not keep an invisible network listener alive.
    receiver.stop().catch(() => {});
  }
  onDestroy(): void {
    receiver.stop().catch(() => {});
  }
}
