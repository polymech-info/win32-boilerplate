import { useEffect } from "react";
import { Link } from "@tanstack/react-router";

import { recordSessionVisited } from "@/home/recentSessions";
import { hasWebProviderHost, postHost } from "@pm/shared/chat/hostBridge";

type Props = { sessionId: string };

export function HomeSessionView({ sessionId }: Props) {
  useEffect(() => {
    recordSessionVisited(sessionId, false);
    if (hasWebProviderHost()) postHost({ kind: "openChatSession", sessionId });
  }, [sessionId]);

  return (
    <article className="panel">
      <h1 className="mt-0 text-lg font-semibold">Opening chat session</h1>
      <p className="muted">The Home app now opens saved sessions in the Chat panel.</p>
      <p className="mt-4">
        <Link to="/" className="link">
          Back to Home
        </Link>
      </p>
    </article>
  );
}
