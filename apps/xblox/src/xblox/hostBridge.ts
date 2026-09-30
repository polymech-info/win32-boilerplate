import type { BlocksFile } from "@/schema/blocks-file";
import type { CustomCommandItem, CustomCommandsDocument } from "@pm/shared/customCommands/types";
import { flattenCommandDocument } from "@pm/shared/customCommands/helpers";
import {
  attachXbloxHostBridge,
  callXbloxHost,
  hasXbloxHost,
  type ProviderRpcResult,
  type XbloxCommandsPayload as SharedXbloxCommandsPayload,
  type XbloxDocumentPayload as SharedXbloxDocumentPayload,
  type XbloxRunEvent,
  type XbloxRunPayload,
} from "@pm/shared/web/hostBridge";

export type XbloxRpcResult<T = unknown> = ProviderRpcResult<T>;

export type XbloxCommandsPayload = SharedXbloxCommandsPayload<CustomCommandItem, CustomCommandsDocument>;

export type XbloxDocumentPayload = SharedXbloxDocumentPayload<BlocksFile, CustomCommandItem, CustomCommandsDocument>;

export { attachXbloxHostBridge, callXbloxHost, flattenCommandDocument, hasXbloxHost };
export type { XbloxRunEvent, XbloxRunPayload };
