type Hunt = {id:string;isRamsgate:boolean;isTrainingDojo:boolean;startTime:Date;readyAt?:number};
export class HuntLifetime {
    private emptySince = new Map<string,{since:number,last:number}>();
    // Keep the former max-age argument compatible; age alone never proves vacancy.
    constructor(_maxAgeMs=120*60000, private emptyMs=2*60000, private startupGraceMs=0) {}
    prune(ids:Set<string>) {for(const id of this.emptySince.keys())if(!ids.has(id))this.emptySince.delete(id);}
    reason(server:Hunt, connections:number|undefined, now=Date.now()): 'empty'|undefined {
        if(server.isRamsgate || server.isTrainingDojo)return;
        if(server.readyAt===undefined || now<server.readyAt+this.startupGraceMs || connections!==0){this.emptySince.delete(server.id);return;}
        let empty=this.emptySince.get(server.id);
        // Missed watchdog observations cannot establish a continuous empty interval.
        if(!empty || now<empty.last || now-empty.last>90000){empty={since:now,last:now};this.emptySince.set(server.id,empty);}
        empty.last=now;
        if(now-empty.since>=this.emptyMs)return 'empty';
    }
}
export function HuntLifetimeFromEnv(env:NodeJS.ProcessEnv=process.env) {
    const minutes=(name:string,fallback:number)=>{
        const value=env[name]===undefined?fallback:Number(env[name]);
        if(!Number.isInteger(value)||value<1||value>1440)throw new Error(`Invalid ${name}`);
        return value*60000;
    };
    return new HuntLifetime(minutes('HUNT_MAX_AGE_MINUTES',120),minutes('HUNT_EMPTY_MINUTES',2),env.HUNT_STARTUP_GRACE_MINUTES===undefined?0:minutes('HUNT_STARTUP_GRACE_MINUTES',1));
}
