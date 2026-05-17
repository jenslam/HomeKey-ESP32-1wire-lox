<script lang="ts">
  import { type LoxoneConfig } from '$lib/types/api';
  import { saveConfig } from '$lib/services/api';
  import { diff } from '$lib/utils/objDiff';

  let { loxone, error }: { loxone: LoxoneConfig | null; error: string | null } = $props();

  // svelte-ignore state_referenced_locally
  let loxoneConfig = $state<LoxoneConfig>($state.snapshot(loxone) ?? { enabled: false, gpioPin: 4, activeDurationMs: 3000 });

  const saveLoxoneConfig = async (e: Event) => {
    e.preventDefault();
    try {
      if (!loxoneConfig || !loxone) return;
      const result = await saveConfig('loxone', diff(loxone as LoxoneConfig, loxoneConfig));
      if (result.success) {
        loxoneConfig = result.data;
        loxone = result.data;
      }
    } catch (e) {
      const message = e instanceof Error ? e.message : String(e);
      alert(`Error saving config: ${message}`);
    }
  };

  const resetForm = () => {
    if (loxone) loxoneConfig = $state.snapshot(loxone);
  };
</script>

<div class="w-full py-6">
  <div class="mb-6">
    <h1 class="text-2xl font-bold text-base-content flex items-center gap-2">
      Loxone 1-Wire
      <div class="tooltip tooltip-bottom tooltip-info" data-tip="Device will reboot to apply changes!">
        <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" class="size-5 text-info">
          <path stroke-linecap="round" stroke-linejoin="round" d="m3.75 13.5 10.5-11.25L12 10.5h8.25L9.75 21.75 12 13.5H3.75Z" />
        </svg>
      </div>
    </h1>
    <p class="text-sm text-base-content/60">Configure the DS1990A iButton emulation for Loxone 1-Wire authentication.</p>
  </div>

  {#if !loxone && error}
    <div class="text-center text-error">
      <p>Error: {error}</p>
    </div>
  {:else if loxoneConfig}
    <form onsubmit={saveLoxoneConfig} class="max-w-2xl space-y-6">

      <!-- Enable toggle -->
      <div class="card bg-base-200 rounded-xl p-4">
        <label class="flex items-center justify-between cursor-pointer">
          <div>
            <span class="font-medium">Enable 1-Wire Bridge</span>
            <p class="text-sm text-base-content/60">Emulate a DS1990A iButton on the 1-Wire bus after each HomeKey tap.</p>
          </div>
          <input
            type="checkbox"
            class="toggle toggle-success ml-4"
            bind:checked={loxoneConfig.enabled}
          />
        </label>
      </div>

      <!-- GPIO and timing -->
      <div class="card bg-base-200 rounded-xl p-4 space-y-4" class:opacity-50={!loxoneConfig.enabled}>
        <div class="form-control">
          <label class="label" for="gpioPin">
            <span class="label-text font-medium">1-Wire GPIO Pin</span>
            <span class="label-text-alt text-base-content/60">Must support open-drain output, not input-only (avoid GPIO 34–39)</span>
          </label>
          <input
            id="gpioPin"
            type="number"
            class="input input-bordered w-32"
            min="0"
            max="33"
            disabled={!loxoneConfig.enabled}
            bind:value={loxoneConfig.gpioPin}
          />
          <label class="label">
            <span class="label-text-alt">Connect an external 4.7 kΩ pull-up resistor to 3.3 V on this pin.</span>
          </label>
        </div>

        <div class="form-control">
          <label class="label" for="activeDuration">
            <span class="label-text font-medium">Active Window (ms)</span>
            <span class="label-text-alt text-base-content/60">How long the iButton is visible after a successful tap</span>
          </label>
          <input
            id="activeDuration"
            type="number"
            class="input input-bordered w-40"
            min="500"
            max="65000"
            step="500"
            disabled={!loxoneConfig.enabled}
            bind:value={loxoneConfig.activeDurationMs}
          />
          <label class="label">
            <span class="label-text-alt">Loxone polls ~every 1 s. 3000 ms (default) gives 2–3 read cycles.</span>
          </label>
        </div>
      </div>

      <!-- How it works info box -->
      <div class="alert alert-info">
        <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" class="stroke-current shrink-0 w-6 h-6">
          <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M13 16h-1v-4h-1m1-4h.01M21 12a9 9 0 11-18 0 9 9 0 0118 0z"></path>
        </svg>
        <div class="text-sm">
          <p class="font-medium mb-1">How iButton codes are generated</p>
          <p>
            Each Apple account has a unique <strong>Issuer ID</strong>. After a successful HomeKey tap the ESP32 derives
            a deterministic 8-byte DS1990A ROM from the first 6 bytes of that ID (family code <code>0x01</code> + 6 bytes + CRC8).
          </p>
          <p class="mt-1">
            To add a new user: tap their device once, then open the Loxone 1-Wire extension in Loxone Config and run
            <em>"Scan for devices"</em>. The iButton ROM will appear automatically. No manual mapping needed.
          </p>
          <p class="mt-1">
            The Issuer ID is the same across all devices on one Apple ID (iPhone, Apple Watch, iPad).
          </p>
        </div>
      </div>

      <!-- Action buttons -->
      <div class="flex gap-3">
        <button type="submit" class="btn btn-primary">Save &amp; Reboot</button>
        <button type="button" class="btn btn-ghost" onclick={resetForm}>Reset</button>
      </div>
    </form>
  {/if}
</div>
