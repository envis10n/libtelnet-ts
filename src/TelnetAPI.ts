import { TelnetFlag } from "./consts";

export interface TelnetAPI {
  onRuntimeInitialized(): void;
  _init(): void;
  _malloc(size: number): number;
  HEAPU8: Uint8Array;
  _telnet_init(compatibilityTable: number, flags: TelnetFlag, userData: number): number;
  _telnet_recv(telnet: number, buffer: number, size: number): void;
  _telnet_free(telnet: number): void;
  _free(pointer: number): void;
}