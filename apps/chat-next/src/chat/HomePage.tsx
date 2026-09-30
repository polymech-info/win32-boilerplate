import { useEffect, useState } from "react";

import { recordSessionVisited } from "@/chat/recentSessions";
import { PmImageChatComposer } from "@/pm-chat/PmImageChatComposer";

/** App home: fresh session id; empty UI is Quick tools (style presets) + composer only. */
export function HomePage() {
  const [sessionId] = useState(() => crypto.randomUUID());

  useEffect(() => {
    recordSessionVisited(sessionId, false);
  }, [sessionId]);

  return (
    <div className="flex h-full min-h-0 flex-1 flex-col">
      <PmImageChatComposer sessionId={sessionId} />
    </div>
  );
}
