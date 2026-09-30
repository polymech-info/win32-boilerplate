import { useEffect } from "react";

import { recordSessionVisited } from "@/chat/recentSessions";
import { PmImageChatComposer } from "@/pm-chat/PmImageChatComposer";

type Props = { sessionId: string };

export function ChatSessionView({ sessionId }: Props) {
  useEffect(() => {
    recordSessionVisited(sessionId, false);
  }, [sessionId]);

  return (
    <div className="flex h-full min-h-0 flex-1 flex-col">
      <PmImageChatComposer sessionId={sessionId} />
    </div>
  );
}
