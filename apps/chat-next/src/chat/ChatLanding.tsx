import { Link, useNavigate, useRouterState } from "@tanstack/react-router";
import { useMemo } from "react";

import { DEMO_SESSION_ID } from "@/chat/demoSession";
import { chatSessionPath } from "@/chat/paths";
import { getSessionSidebarRows, recordSessionVisited } from "@/chat/recentSessions";
type Variant = "home" | "chat";

type Props = { variant: Variant };

/**
 * Chat hub (`/chat`): new chat + recent sessions. App `/` uses Quick tools + composer only.
 */
export function ChatLanding({ variant }: Props) {
  const navigate = useNavigate();
  const pathname = useRouterState({ select: (s) => s.location.pathname });
  const sessionRows = useMemo(() => getSessionSidebarRows({ omitDemo: false }), [pathname]);

  function newSession() {
    const sessionId = crypto.randomUUID();
    recordSessionVisited(sessionId);
    void navigate({ to: "/chat/$sessionId", params: { sessionId } });
  }

  return (
    <section className="panel max-w-2xl">
      <div className="flex flex-col gap-2">
        <div className="flex flex-wrap items-center gap-3">
          <button type="button" className="btn" onClick={() => newSession()}>
            New chat
          </button>
          {variant === "chat" ? (
            <Link to="/" className="link text-sm">
              Home
            </Link>
          ) : null}
        </div>
        <ul className="m-0 flex flex-wrap gap-x-4 gap-y-1 p-0 text-sm">
          {sessionRows.map(({ id, label }) => (
            <li key={id} className="list-none">
              <Link to="/chat/$sessionId" params={{ sessionId: id }} className="link">
                {label}
              </Link>
            </li>
          ))}
        </ul>
      </div>

      <p className="muted mt-4 text-xs">
        <code className="text-xs">{chatSessionPath(DEMO_SESSION_ID)}</code>
      </p>
    </section>
  );
}
